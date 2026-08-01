extends Node

## The whole map pipeline against data it has never seen.
##
## Every defect worth finding this session came from running against something real that had
## not been run against before. Condition Zero and its Deleted Scenes are the newest such
## thing on this machine: Deleted Scenes is a single-player campaign, so its levels carry
## scripted sequences, ambience and entity types a Counter-Strike bomb map never has.
##
## So this takes maps from those two and asks everything at once: does the geometry come out,
## are the surfaces textured, does the sky load, are the entities readable, do the sounds it
## declares resolve. One map failing one of those is a lead; all of them failing one is a bug.

const SkyLoader := preload("res://addons/quebratsk_editor/sky_loader.gd")

var _unresolved := {}
var _vfs: VFSManager
var _importer: UnifiedAssetImporter


func _ready() -> void:
	_vfs = VFSManager.new()
	_importer = UnifiedAssetImporter.new()
	add_child(_vfs)
	add_child(_importer)
	_importer.set_vfs(_vfs)

	var games: Dictionary = SteamLibraryDetector.detect_installed_games()
	var n := 0
	for title in games:
		var root := str(games[title])
		var scan: Dictionary = _vfs.scan_game_directory(root)
		for archive in scan.get("archives", []):
			if _vfs.mount_container("g%d" % n, str(archive)):
				n += 1
		_vfs.mount_directory("loose%d" % n, root)
		n += 1

	print("\n=== CONDITION ZERO MAPS ===")

	var maps := PackedStringArray()
	for needle in ["czeror/maps/", "czero/maps/"]:
		var hit: Dictionary = _vfs.find_files(needle, PackedStringArray(["bsp"]),
			PackedStringArray(), PackedStringArray(), 0)
		for entry in hit["files"]:
			maps.append(str(entry))
	print("%d map(s) found in Condition Zero and its Deleted Scenes" % maps.size())
	if maps.is_empty():
		print("nothing to check")
		get_tree().quit()
		return

	var checked := 0
	var fully_textured := 0
	var with_sky := 0
	var sounds_declared := 0
	var sounds_found := 0
	var worst := 100.0
	var worst_name := ""

	for entry in maps:
		if checked >= 8:
			break
		var result: Dictionary = _check(str(entry))
		if result.is_empty():
			continue
		checked += 1
		var pct: float = result["textured_pct"]
		if pct >= 99.0:
			fully_textured += 1
		if pct < worst:
			worst = pct
			worst_name = str(entry).get_file()
		if result["sky"]:
			with_sky += 1
		sounds_declared += int(result["sounds"])
		sounds_found += int(result["sounds_found"])

	print("\n=== SUMMARY ===")
	print("  %d maps read" % checked)
	print("  %d textured at 99%% or better, worst is %s at %.0f%%"
		% [fully_textured, worst_name, worst])
	print("  %d load their sky" % with_sky)
	print("  %d of %d declared sounds reach a file" % [sounds_found, sounds_declared])
	if not _unresolved.is_empty():
		var names := _unresolved.keys()
		names.sort()
		print("  what did not: %s" % ", ".join(PackedStringArray(names)))
	get_tree().quit()


func _check(uri: String) -> Dictionary:
	var map: Node3D = _importer.load_map(uri)
	if map == null:
		print("\n%s   ** would not load **" % uri.get_file())
		return {}

	var mesh: ArrayMesh = (map as MeshInstance3D).mesh as ArrayMesh
	if mesh == null:
		map.queue_free()
		print("\n%s   ** no geometry **" % uri.get_file())
		return {}

	var textured := 0
	for i in mesh.get_surface_count():
		var mat := mesh.surface_get_material(i) as StandardMaterial3D
		if mat != null and mat.albedo_texture != null:
			textured += 1
	var pct := 0.0 if mesh.get_surface_count() == 0 \
		else 100.0 * textured / mesh.get_surface_count()

	var entities: Array = _importer.load_map_entities(uri)
	var kinds := {}
	for e in entities:
		var cls := str((e as Dictionary).get("classname", ""))
		kinds[cls] = int(kinds.get(cls, 0)) + 1

	# The sky is the difference between a place and a model of a place, and its six images
	# live outside the .bsp, so it is the part most likely to be absent on a given machine.
	var absent: Array = []
	var sky_name := SkyLoader.sky_name(entities)
	var sky := SkyLoader.load_sky(_vfs, entities, absent)

	var declared := 0
	var found := 0
	for e in entities:
		var entity: Dictionary = e
		if str(entity.get("classname", "")) != "ambient_generic":
			continue
		var message := str(entity.get("message", ""))
		if message.is_empty():
			continue
		declared += 1
		if not _importer.resolve_sound(message, uri).is_empty():
			found += 1
		elif not _unresolved.has(message):
			_unresolved[message] = true

	print("\n%s" % uri.get_file())
	print("   %d surfaces, %d textured (%.0f%%)"
		% [mesh.get_surface_count(), textured, pct])
	print("   %d entities of %d kinds, %d ambient_generic (%d resolve)"
		% [entities.size(), kinds.size(), declared, found])
	if sky_name.is_empty():
		print("   names no sky")
	elif sky != null:
		print("   sky '%s' loaded" % sky_name)
	else:
		print("   sky '%s' missing: %s" % [sky_name, ", ".join(PackedStringArray(absent))])

	# The entity kinds a Counter-Strike map never has are the reason this map was chosen.
	var interesting := []
	for cls in kinds:
		if str(cls).begins_with("scripted_") or str(cls).begins_with("monster_") \
				or str(cls).begins_with("trigger_") or str(cls) == "env_sound":
			interesting.append("%s x%d" % [cls, kinds[cls]])
	if not interesting.is_empty():
		interesting.sort()
		print("   campaign entities: %s" % ", ".join(PackedStringArray(interesting.slice(0, 6))))

	map.queue_free()
	return {
		"textured_pct": pct,
		"sky": sky != null,
		"sounds": declared,
		"sounds_found": found,
	}
