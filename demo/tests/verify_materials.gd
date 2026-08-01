extends Node

## Do imported models actually get their textures?
##
## A model that renders flat and grey looks exactly like a model with no textures, and both
## look like success from the outside: load_model() returns a node either way. This counts
## how many surfaces come back with a real image on them, across a sample of models from
## every mounted game, and names what could not be found.

const SAMPLE := 40

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

	print("\n=== MATERIAL VERIFICATION ===")
	_check("models/player/", "characters")
	_check("weapons/", "weapons")
	_check("props", "props")
	print("\n=== DONE ===")
	get_tree().quit()


func _check(needle: String, label: String) -> void:
	var hit: Dictionary = _vfs.find_files(needle, PackedStringArray(["mdl"]),
		PackedStringArray(["vvd", "vtx", "ani", "phy"]), PackedStringArray(), 0)

	var models := 0
	var surfaces := 0
	var textured := 0
	var fully := 0
	var absent := {}

	for entry in hit["files"]:
		if models >= SAMPLE:
			break
		var node: Node3D = _importer.load_model(str(entry))
		if node == null:
			continue
		models += 1

		var mesh: ArrayMesh = _find_mesh(node)
		if mesh != null:
			var with_image := 0
			for i in mesh.get_surface_count():
				surfaces += 1
				var mat := mesh.surface_get_material(i) as StandardMaterial3D
				if mat != null and mat.albedo_texture != null:
					textured += 1
					with_image += 1
			if with_image == mesh.get_surface_count() and mesh.get_surface_count() > 0:
				fully += 1

		for missing in _importer.get_last_missing_companions():
			var name := str(missing)
			# The .vvd/.vtx entries are a different kind of missing and are already
			# reported elsewhere; this check is about materials.
			if not (name.ends_with(".vvd") or name.ends_with(".vtx")):
				absent[name] = int(absent.get(name, 0)) + 1
		node.queue_free()

	print("\n%s: %d models, %d surfaces" % [label, models, surfaces])
	if surfaces > 0:
		print("   %d of %d surfaces have an image (%.0f%%)"
			% [textured, surfaces, 100.0 * textured / surfaces])
		print("   %d of %d models are fully textured" % [fully, models])
	if absent.is_empty():
		print("   nothing was missing")
	else:
		var names := absent.keys()
		names.sort()
		print("   %d distinct materials not found, e.g." % names.size())
		for i in mini(5, names.size()):
			print("      %s (wanted by %d model(s))" % [names[i], absent[names[i]]])


func _find_mesh(node: Node) -> ArrayMesh:
	if node is MeshInstance3D:
		return (node as MeshInstance3D).mesh as ArrayMesh
	for child in node.get_children():
		var found := _find_mesh(child)
		if found != null:
			return found
	return null
