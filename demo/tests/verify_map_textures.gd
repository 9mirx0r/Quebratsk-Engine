extends Node

## Do imported maps get their textures?
##
## A GoldSrc map stores its textures one of two ways: embedded in the .bsp itself, or named
## and left in a .wad that worldspawn lists by filename. The second kind is the interesting
## one, because whether it resolves depends on which archives happen to be mounted, and a
## surface whose texture was not found renders as a flat colour that looks like a texture
## nobody bothered with.
##
## This counts both, per map, and names what could not be found.

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

	print("\n=== MAP TEXTURE VERIFICATION ===")

	var hit: Dictionary = _vfs.find_files("de_survivor", PackedStringArray(["bsp"]),
		PackedStringArray(), PackedStringArray(), 0)

	var checked := 0
	for entry in hit["files"]:
		if checked >= 8:
			break
		if _check(str(entry)):
			checked += 1

	print("\n=== DONE ===")
	get_tree().quit()


func _check(uri: String) -> bool:
	var map: Node3D = _importer.load_map(uri)
	if map == null:
		return false

	var mesh: ArrayMesh = (map as MeshInstance3D).mesh as ArrayMesh
	if mesh == null:
		map.queue_free()
		return false

	var textured := 0
	for i in mesh.get_surface_count():
		var mat := mesh.surface_get_material(i) as StandardMaterial3D
		if mat != null and mat.albedo_texture != null:
			textured += 1

	# What worldspawn asked for. A map lists the .wad files it expects beside it, and if
	# those are not mounted every texture in them is missing at once, which is a very
	# different problem from one texture being absent.
	var wads := ""
	for e in _importer.load_map_entities(uri):
		var entity: Dictionary = e
		if str(entity.get("classname", "")) == "worldspawn":
			wads = str(entity.get("wad", ""))
			break

	var wanted := []
	for w in wads.split(";", false):
		var name := str(w).replace("\\", "/").get_file()
		if not name.is_empty():
			wanted.append(name)

	var present := 0
	for w in wanted:
		if not _vfs.find_files(str(w).get_basename(), PackedStringArray(["wad"]),
				PackedStringArray(), PackedStringArray(), 1)["files"].is_empty():
			present += 1

	print("\n%s" % uri.get_file())
	print("   %d of %d surfaces textured (%.0f%%)"
		% [textured, mesh.get_surface_count(),
		   0.0 if mesh.get_surface_count() == 0 else 100.0 * textured / mesh.get_surface_count()])
	if wanted.is_empty():
		print("   worldspawn lists no .wad files, so its textures are embedded")
	else:
		print("   worldspawn wants %d .wad file(s), %d of them are mounted" % [wanted.size(), present])
		if present < wanted.size():
			var absent := []
			for w in wanted:
				if _vfs.find_files(str(w).get_basename(), PackedStringArray(["wad"]),
						PackedStringArray(), PackedStringArray(), 1)["files"].is_empty():
					absent.append(str(w))
			print("      missing: %s" % ", ".join(PackedStringArray(absent)))

	map.queue_free()
	return true
