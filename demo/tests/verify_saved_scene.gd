extends Node

const SurfaceMaterial := preload("res://addons/quebratsk_editor/surface_material.gd")

const GamePaths := preload("res://tests/game_paths.gd")

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

	_dock._add_game_folder(GamePaths.common("Half-Life"),
		"Half-Life")
	_dock._add_game_folder(GamePaths.common("GarrysMod/garrysmod"),
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

	# A level has things a model does not: baked light shared by every surface, many meshes,
	# and the bodies you collide with.
	var maps_checked := 0
	var maps_survived := 0
	for uri in _map_candidates():
		maps_checked += 1
		if _map_round_trip(str(uri)):
			maps_survived += 1

	print("\n=== SUMMARY ===")
	print("  %d of %d saved scenes reload with everything they had" % [survived, checked])
	print("  %d of %d saved levels reload lit and solid" % [maps_survived, maps_checked])
	var lost := (checked - survived) + (maps_checked - maps_survived)
	if lost > 0:
		print("  a saved scene loses something on the way to disk  ** FAILED **")
	get_tree().quit(1 if lost > 0 else 0)


## Two Half-Life levels, one small and one with plenty of brush entities.
func _map_candidates() -> PackedStringArray:
	var out := PackedStringArray()
	for needle in ["valve/maps/c0a0.bsp", "valve/maps/crossfire.bsp"]:
		var hit: Dictionary = _dock._vfs.find_files(needle, PackedStringArray(["bsp"]),
			PackedStringArray(), PackedStringArray(), 4)
		for entry in hit["files"]:
			out.append(str(entry))
			break
	return out


func _map_round_trip(uri: String) -> bool:
	# Built the way the dock's Save button builds it.
	var live: Node3D = _dock._build_for_scene(uri, true)
	if live == null:
		print("\n%s   would not load  ** FAILED **" % uri.get_file())
		return false
	add_child(live)
	var before: Dictionary = _describe_map(live)

	var base := uri.get_file().get_basename()
	DirAccess.make_dir_recursive_absolute(SAVE_DIR)
	_dock._externalise(live, base)
	_dock._claim_ownership(live, live)

	var packed := PackedScene.new()
	var path := "%s/%s.tscn" % [SAVE_DIR, base.validate_filename()]
	var saved := packed.pack(live) == OK and ResourceSaver.save(packed, path) == OK
	remove_child(live)
	live.free()
	if not saved:
		print("\n%s   could not be written  ** FAILED **" % uri.get_file())
		return false

	var file := FileAccess.open(path, FileAccess.READ)
	var bytes := 0 if file == null else file.get_length()
	if file != null:
		file.close()

	var scene: PackedScene = ResourceLoader.load(path, "PackedScene",
		ResourceLoader.CACHE_MODE_IGNORE_DEEP)
	var reborn: Node3D = null if scene == null else scene.instantiate() as Node3D
	if reborn == null:
		print("\n%s   would not load back  ** FAILED **" % uri.get_file())
		return false
	add_child(reborn)
	var after: Dictionary = _describe_map(reborn)

	print("\n%s   %.1f KiB of scene on disk" % [uri.get_file(), bytes / 1024.0])
	var ok := true
	for key in ["meshes", "surfaces", "textured", "lit", "bodies", "shapes", "player_bodies"]:
		var same: bool = after[key] == before[key]
		print("   %-14s %6d -> %6d%s" % [key, before[key], after[key],
			"" if same else "  ** FAILED **"])
		ok = ok and same
	# Lit at all, and solid at all: equal counts of nothing would pass the lines above.
	for key in ["lit", "shapes", "player_bodies"]:
		if before[key] == 0:
			print("   nothing to lose under %s  ** FAILED **" % key)
			ok = false
	# The light is one picture, in one file, however many surfaces use it.
	var lightmaps: Dictionary = after["lightmap_paths"]
	if lightmaps.size() != 1 or not str(lightmaps.keys()[0]).begins_with(SAVE_DIR):
		print("   baked light should be one file beside the scene, found %s  ** FAILED **"
			% str(lightmaps.keys()))
		ok = false
	if after["loose_textures"] > 0 or after["loose_meshes"] > 0:
		print("   %d texture(s) and %d mesh(es) were written into the scene file  ** FAILED **"
			% [after["loose_textures"], after["loose_meshes"]])
		ok = false

	remove_child(reborn)
	reborn.free()
	DirAccess.remove_absolute(ProjectSettings.globalize_path(path))
	return ok


## What an imported level is made of, counted the same way before and after.
func _describe_map(node: Node3D) -> Dictionary:
	var out := {
		"meshes": 0, "surfaces": 0, "textured": 0, "lit": 0, "bodies": 0, "shapes": 0,
		"player_bodies": 0, "lightmap_paths": {}, "loose_textures": 0, "loose_meshes": 0,
	}
	for child in _every_node(node):
		if child is MeshInstance3D:
			var mesh: ArrayMesh = (child as MeshInstance3D).mesh as ArrayMesh
			if mesh == null:
				continue
			out["meshes"] += 1
			if mesh.resource_path.is_empty():
				out["loose_meshes"] += 1
			for i in mesh.get_surface_count():
				out["surfaces"] += 1
				var mat := mesh.surface_get_material(i)
				var texture := SurfaceMaterial.texture_of(mat)
				if texture != null:
					out["textured"] += 1
					if texture.resource_path.is_empty():
						out["loose_textures"] += 1
				var light := SurfaceMaterial.lightmap_of(mat)
				if light != null:
					out["lit"] += 1
					out["lightmap_paths"][light.resource_path] = true
		elif child is StaticBody3D:
			out["bodies"] += 1
			# The layer is what keeps the player's invisible walls away from everything else.
			if child.name == "PlayerClip" and (child as StaticBody3D).collision_layer == 1 << 19 \
					and (child as StaticBody3D).collision_mask == 0:
				out["player_bodies"] += 1
		elif child is CollisionShape3D:
			if (child as CollisionShape3D).shape != null:
				out["shapes"] += 1
	return out


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
		print("   something did not survive  ** FAILED **")

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
					if SurfaceMaterial.texture_of(mesh.surface_get_material(i)) != null:
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
