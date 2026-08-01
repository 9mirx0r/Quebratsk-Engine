extends Node

## Does a saved scene still work after Godot reloads it?
##
## Saving is the step that decides whether Quebratsk is a browser or something a game can be
## built with. A game cannot mount several hundred game archives at startup and rebuild every
## model from bytes: it loads Godot resources. So the import has to survive being written to
## disk and read back by an engine that knows nothing about .mdl files.
##
## Everything an import produces is built at runtime and belongs to no file: the ArrayMesh,
## the ImageTextures, the Skin, the AnimationLibrary. Whether PackedScene carries all of that
## across is not something the code can be read to find out, and a scene that reloads as an
## empty node reports no error at all. So the test is to save, forget, load, and compare.

const DockScript := preload("res://addons/quebratsk_editor/quebratsk_dock.gd")
const SAVE_DIR := "res://imported"

var _dock: Node


func _ready() -> void:
	DirAccess.remove_absolute(ProjectSettings.globalize_path(DockScript.MOUNTS_FILE))
	_dock = DockScript.new()
	add_child(_dock)
	await get_tree().process_frame

	_dock._add_game_folder("C:/Program Files (x86)/Steam/steamapps/common/Half-Life",
		"Half-Life")
	_dock._add_game_folder("C:/Program Files (x86)/Steam/steamapps/common/GarrysMod/garrysmod",
		"Garry's Mod")

	print("\n=== SAVED SCENE VERIFICATION ===")

	var checked := 0
	var survived := 0
	for uri in _candidates():
		if checked >= 4:
			break
		var result: Dictionary = _round_trip(str(uri))
		if result.is_empty():
			continue
		checked += 1
		if result["ok"]:
			survived += 1

	print("\n=== SUMMARY ===")
	print("  %d of %d saved scenes reload with everything they had" % [survived, checked])
	if checked > 0 and survived < checked:
		print("  ** a saved scene loses something on the way to disk **")
	get_tree().quit()


## Models worth the test: textured, skinned and animated, so there is something to lose.
func _candidates() -> PackedStringArray:
	# Bounded hard. Asking a model how many sequences it has means parsing it, and there are
	# tens of thousands indexed: an unbounded scan for eight good ones never finishes.
	var out := PackedStringArray()
	var looked := 0
	for needle in ["models/player/", "models/"]:
		var hit: Dictionary = _dock._vfs.find_files(needle, PackedStringArray(["mdl"]),
			PackedStringArray(["vvd", "vtx", "ani", "phy"]), PackedStringArray(), 60)
		for entry in hit["files"]:
			looked += 1
			if looked > 60:
				break
			if _dock._importer.list_poses(str(entry)).size() >= 20:
				out.append(str(entry))
			if out.size() >= 6:
				return out
	return out


func _round_trip(uri: String) -> Dictionary:
	var poses: PackedStringArray = _dock._importer.list_poses(uri)
	var wanted := ""
	for hint in ["walk", "run", "idle"]:
		for p in poses:
			if str(p).to_lower().begins_with(hint):
				wanted = str(p)
				break
		if not wanted.is_empty():
			break
	if wanted.is_empty():
		return {}

	# Built the way the dock builds it, animation and all, so this measures the real path.
	var live: Node3D = _dock._importer.load_model(uri, wanted, PackedStringArray([wanted]))
	if live == null:
		return {}
	add_child(live)
	var before: Dictionary = _describe(live)

	DirAccess.make_dir_recursive_absolute(SAVE_DIR)
	_dock._externalise(live, uri.get_file().get_basename())
	_dock._claim_ownership(live, live)

	var packed := PackedScene.new()
	if packed.pack(live) != OK:
		live.queue_free()
		print("\n%s   ** pack() refused it **" % uri.get_file())
		return {"ok": false}

	var path := "%s/%s.tscn" % [SAVE_DIR, uri.get_file().get_basename().validate_filename()]
	var saved := ResourceSaver.save(packed, path)
	live.queue_free()
	if saved != OK:
		print("\n%s   ** could not be written **" % uri.get_file())
		return {"ok": false}

	var bytes := 0
	var file := FileAccess.open(path, FileAccess.READ)
	if file != null:
		bytes = file.get_length()
		file.close()

	# Read back through the resource loader with nothing cached, which is what a game does
	# on a machine that has none of these games installed.
	var scene: PackedScene = ResourceLoader.load(path, "PackedScene",
		ResourceLoader.CACHE_MODE_IGNORE_DEEP)
	if scene == null:
		print("\n%s   ** would not load back **" % uri.get_file())
		return {"ok": false}

	var reborn := scene.instantiate() as Node3D
	if reborn == null:
		print("\n%s   ** loaded as nothing **" % uri.get_file())
		return {"ok": false}
	add_child(reborn)
	var after: Dictionary = _describe(reborn)

	var ok: bool = after["surfaces"] == before["surfaces"] \
		and after["textured"] == before["textured"] \
		and after["bones"] == before["bones"] \
		and after["animations"] == before["animations"] \
		and after["keys"] == before["keys"]

	print("\n%s   %.1f KiB on disk" % [uri.get_file(), bytes / 1024.0])
	print("   surfaces %d -> %d, textured %d -> %d, bones %d -> %d"
		% [before["surfaces"], after["surfaces"], before["textured"], after["textured"],
		   before["bones"], after["bones"]])
	print("   animations %d -> %d, richest track %d keys -> %d"
		% [before["animations"], after["animations"], before["keys"], after["keys"]])
	if not ok:
		print("   ** something did not survive **")

	reborn.queue_free()
	DirAccess.remove_absolute(ProjectSettings.globalize_path(path))
	return {"ok": ok}


## What an imported model is made of, counted the same way before and after.
func _describe(node: Node3D) -> Dictionary:
	var surfaces := 0
	var textured := 0
	var bones := 0
	var animations := 0
	var keys := 0

	for child in _every_node(node):
		if child is Skeleton3D:
			bones = maxi(bones, (child as Skeleton3D).get_bone_count())
		elif child is MeshInstance3D:
			var mesh: ArrayMesh = (child as MeshInstance3D).mesh as ArrayMesh
			if mesh != null:
				surfaces += mesh.get_surface_count()
				for i in mesh.get_surface_count():
					var mat := mesh.surface_get_material(i) as StandardMaterial3D
					if mat != null and mat.albedo_texture != null:
						textured += 1
		elif child is AnimationPlayer:
			var player := child as AnimationPlayer
			for name in player.get_animation_list():
				animations += 1
				var anim: Animation = player.get_animation(str(name))
				for t in anim.get_track_count():
					keys = maxi(keys, anim.track_get_key_count(t))

	return {
		"surfaces": surfaces, "textured": textured, "bones": bones,
		"animations": animations, "keys": keys,
	}


func _every_node(root: Node) -> Array:
	var out := [root]
	for child in root.get_children():
		out.append_array(_every_node(child))
	return out
