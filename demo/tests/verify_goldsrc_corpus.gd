extends Node

const SurfaceMaterial := preload("res://addons/quebratsk_editor/surface_material.gd")

## The GoldSrc import path against every game folder installed, with real pass/fail.
##
## Most suites here print what they find and leave the judging to a reader. This one judges.
## It exists because of a defect that went unseen for exactly that reason: all 37 Blue Shift
## maps were read with two lumps swapped, so their entity text was plane data, and nothing
## failed because nothing checked.
##
## Three questions, each over every GoldSrc game folder found:
##
##   1. Entities. Every map must yield an entity list whose first entry is `worldspawn`.
##      Cheap, so it runs on all maps rather than a sample.
##   2. Geometry. A sample of maps per game must load with surfaces, and most of those
##      surfaces must have found their texture.
##   3. Models. A sample of models per game must build a node.
##
## Any line ending in ** FAILED ** fails the run (see tools/run_godot_suites.py), and the
## scene itself exits 1.

const GamePaths := preload("res://tests/game_paths.gd")

const MAPS_PER_GAME := 3
const MODELS_PER_GAME := 4
## Below this share of textured surfaces a map is broken rather than merely missing a WAD.
const MIN_TEXTURED_PCT := 50.0

var _vfs: VFSManager
var _importer: UnifiedAssetImporter
var _failures := 0


func _fail(message: String) -> void:
	_failures += 1
	print("   %s  ** FAILED **" % message)


func _ready() -> void:
	_vfs = VFSManager.new()
	_importer = UnifiedAssetImporter.new()
	add_child(_vfs)
	add_child(_importer)
	_importer.set_vfs(_vfs)

	print("\n=== GOLDSRC CORPUS ===")
	var root := GamePaths.common("Half-Life")
	if not GamePaths.installed(root):
		print("SKIP: Half-Life is not installed (looked in %s)" % root)
		get_tree().quit()
		return

	var n := 0
	var scan: Dictionary = _vfs.scan_game_directory(root)
	for archive in scan.get("archives", []):
		if _vfs.mount_container("g%d" % n, str(archive)):
			n += 1
	_vfs.mount_directory("hl", root)
	print("%s: %d archive(s) mounted" % [root, n])

	var by_game := _group_by_game("maps/", "bsp")
	if by_game.is_empty():
		_fail("no .bsp found under %s" % root)
	_check_entities(by_game)
	_check_geometry(by_game)
	_check_models(_group_by_game("models/", "mdl"))

	print("\n=== SUMMARY ===")
	if _failures == 0:
		print("all checks passed")
	else:
		print("%d check(s) failed" % _failures)
	get_tree().quit(1 if _failures > 0 else 0)


## vfs://hl/<game>/<folder>... -> { game: [uri, ...] }, only loose files of the install.
func _group_by_game(folder: String, extension: String) -> Dictionary:
	var hit: Dictionary = _vfs.find_files(folder, PackedStringArray([extension]),
		PackedStringArray(), PackedStringArray(), 0)
	var grouped := {}
	for entry in hit.get("files", []):
		var uri := str(entry)
		if not uri.begins_with("vfs://hl/"):
			continue
		var parts := uri.trim_prefix("vfs://hl/").split("/")
		if parts.size() < 3 or parts[1] + "/" != folder:
			continue
		if not grouped.has(parts[0]):
			grouped[parts[0]] = []
		grouped[parts[0]].append(uri)
	for game in grouped:
		grouped[game].sort()
	return grouped


func _check_entities(by_game: Dictionary) -> void:
	print("\n1. Entities of every map")
	var games := by_game.keys()
	games.sort()
	var total := 0
	for game in games:
		var bad := 0
		var first_bad := ""
		for uri in by_game[game]:
			total += 1
			var entities: Array = _importer.load_map_entities(str(uri))
			var ok := not entities.is_empty() \
				and str((entities[0] as Dictionary).get("classname", "")) == "worldspawn"
			if not ok:
				bad += 1
				if first_bad.is_empty():
					first_bad = str(uri).get_file()
		if bad == 0:
			print("   %-12s %3d map(s), all start with worldspawn" % [game, by_game[game].size()])
		else:
			_fail("%s: %d of %d map(s) have no readable entities, e.g. %s"
				% [game, bad, by_game[game].size(), first_bad])
	print("   %d map(s) read" % total)


func _check_geometry(by_game: Dictionary) -> void:
	print("\n2. Geometry and textures, %d map(s) per game" % MAPS_PER_GAME)
	var games := by_game.keys()
	games.sort()
	for game in games:
		var maps: Array = by_game[game]
		# Spread the sample across the list instead of taking whatever sorts first.
		var step: int = maxi(1, maps.size() / MAPS_PER_GAME)
		var taken := 0
		var index := 0
		while index < maps.size() and taken < MAPS_PER_GAME:
			_check_one_map(str(game), str(maps[index]))
			taken += 1
			index += step


func _check_one_map(game: String, uri: String) -> void:
	var label := "%s/%s" % [game, uri.get_file()]
	var map: Node3D = _importer.load_map(uri)
	if map == null:
		_fail("%s would not load (error %d)" % [label, _importer.get_last_error_code()])
		return
	var mesh_instance := map as MeshInstance3D
	var mesh: ArrayMesh = mesh_instance.mesh as ArrayMesh if mesh_instance else null
	if mesh == null or mesh.get_surface_count() == 0:
		_fail("%s has no geometry" % label)
		map.free()
		return

	var textured := 0
	for i in mesh.get_surface_count():
		if SurfaceMaterial.texture_of(mesh.surface_get_material(i)) != null:
			textured += 1
	var pct := 100.0 * textured / mesh.get_surface_count()
	if pct < MIN_TEXTURED_PCT:
		_fail("%s: only %d of %d surfaces textured (%.0f%%)"
			% [label, textured, mesh.get_surface_count(), pct])
	else:
		print("   %-34s %4d surfaces, %3.0f%% textured" % [label, mesh.get_surface_count(), pct])

	# Every level is made of something: its solid blocks, one convex shape each.
	var body: StaticBody3D = map.get_node_or_null("StaticBody3D")
	var blocks := 0
	var broken := 0
	if body != null:
		for child in body.get_children():
			var col := child as CollisionShape3D
			var convex := col.shape as ConvexPolygonShape3D if col != null else null
			if convex == null or convex.points.size() < 4:
				broken += 1
			else:
				blocks += 1
	if blocks == 0 or broken > 0:
		_fail("%s: %d solid block(s), %d unusable collider(s)" % [label, blocks, broken])
	map.free()


func _check_models(by_game: Dictionary) -> void:
	print("\n3. Models, %d per game" % MODELS_PER_GAME)
	var games := by_game.keys()
	games.sort()
	for game in games:
		var models: Array = by_game[game]
		var step: int = maxi(1, models.size() / MODELS_PER_GAME)
		var taken := 0
		var built := 0
		var index := 0
		while index < models.size() and taken < MODELS_PER_GAME:
			var uri := str(models[index])
			index += step
			# Texture and sequence-group companions are not models in their own right.
			var stem := uri.get_file().get_basename().to_lower()
			if stem.ends_with("t") or stem.right(2).is_valid_int():
				index -= step - 1
				continue
			taken += 1
			var node: Node3D = _importer.load_model(uri)
			if node == null:
				_fail("%s/%s built no node (error %d)"
					% [game, uri.get_file(), _importer.get_last_error_code()])
			else:
				built += 1
				node.free()
		print("   %-12s %d of %d sampled model(s) built" % [game, built, taken])
