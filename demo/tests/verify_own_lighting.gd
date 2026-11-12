extends Node3D

## Can a level's painted light be taken off and put back without losing anything?
##
## Loads one real level, unpaints it, and checks that every lit surface now carries a plain
## material with the same texture, that lamps appear where the level had its own, and that
## repainting leaves it exactly as it arrived. Also checks two things the importer does for
## brush entities: the ones the level asks never to draw are hidden, and the ones stored
## around a pivot are moved to where their entity puts them.

const OwnLighting := preload("res://addons/quebratsk_editor/own_lighting.gd")
const SurfaceMaterial := preload("res://addons/quebratsk_editor/surface_material.gd")

const MAPS := ["dod/maps/dod_anzio.bsp", "valve/maps/crossfire.bsp"]

var _failures := 0


func _check(what: String, ok: bool) -> void:
	if ok:
		print("  ok      %s" % what)
	else:
		_failures += 1
		print("  WRONG   %s" % what)


func _lit_surfaces(map: Node) -> int:
	var count := 0
	for instance in OwnLighting._mesh_instances(map):
		for i in instance.mesh.get_surface_count():
			if instance.get_surface_override_material(i) == null \
					and SurfaceMaterial.lightmap_of(instance.mesh.surface_get_material(i)) != null:
				count += 1
	return count


func _ready() -> void:
	print("=== verify_own_lighting ===")
	var vfs := VFSManager.new()
	var importer := UnifiedAssetImporter.new()
	add_child(vfs)
	add_child(importer)
	importer.set_vfs(vfs)
	var games: Dictionary = SteamLibraryDetector.detect_installed_games()
	var n := 0
	for title in games:
		var root := str(games[title])
		var scan: Dictionary = vfs.scan_game_directory(root)
		for archive in scan.get("archives", []):
			if vfs.mount_container("g%d" % n, str(archive)):
				n += 1
		vfs.mount_directory("loose%d" % n, root)
		n += 1

	var checked := 0
	for needle in MAPS:
		var hit: Dictionary = vfs.find_files(needle, PackedStringArray(["bsp"]),
			PackedStringArray(), PackedStringArray(), 0)
		var files: Array = hit.get("files", [])
		if files.is_empty():
			print("SKIP: %s is not installed" % needle)
			continue
		var uri := str(files[0])
		print(uri)
		var map: Node3D = importer.load_map(uri)
		var entities: Array = importer.load_map_entities(uri)
		if map == null:
			_check("the level loads", false)
			continue
		add_child(map)
		checked += 1

		var before := _lit_surfaces(map)
		_check("the level arrives with painted light (%d surfaces)" % before, before > 0)
		var changed := OwnLighting.unpaint(map)
		_check("every painted surface is unpainted", changed == before and _lit_surfaces(map) == 0)

		var same_texture := true
		for instance in OwnLighting._mesh_instances(map):
			for i in instance.mesh.get_surface_count():
				var plain := instance.get_surface_override_material(i) as StandardMaterial3D
				if plain == null:
					continue
				if plain.albedo_texture != SurfaceMaterial.texture_of(instance.mesh.surface_get_material(i)):
					same_texture = false
		_check("each plain surface keeps its texture", same_texture)

		var wanted := 0
		for e in entities:
			var kind := str(e.get("classname", ""))
			if kind == "light" or kind == "light_spot":
				wanted += 1
		var lamps := OwnLighting.add_lamps(map, entities)
		_check("lamps stand in for the level's own (%d of %d, the rest start off)"
			% [lamps.get_child_count(), wanted],
			lamps.get_child_count() <= wanted and (wanted == 0 or lamps.get_child_count() > 0))

		var restored := OwnLighting.repaint(map)
		_check("repainting puts every surface back", restored == before and _lit_surfaces(map) == before)

		# Brush entities: hidden when the level asks, and placed by their own origin.
		var asked_hidden := 0
		var hidden_right := true
		var placed_right := true
		var pivoting := 0
		for e in entities:
			var model := str(e.get("model", ""))
			if not model.begins_with("*"):
				continue
			var node := map.get_node_or_null("brush_" + model.substr(1)) as Node3D
			if node == null:
				continue
			var unseen := int(str(e.get("rendermode", "0"))) != 0 \
				and float(str(e.get("renderamt", "0"))) <= 0.0
			if unseen:
				asked_hidden += 1
			if node.visible == unseen:
				hidden_right = false
			if e.has("origin"):
				pivoting += 1
				var o := str(e["origin"]).split_floats(" ", false)
				var where := Vector3(-o[1], o[2], -o[0]) * 0.0254
				if node.position.distance_to(where) > 0.01:
					placed_right = false
			elif node.position != Vector3.ZERO:
				placed_right = false
		_check("brush entities the level never draws are hidden (%d)" % asked_hidden, hidden_right)
		_check("brush entities with a pivot stand where it goes (%d)" % pivoting, placed_right)
		map.queue_free()

	if checked == 0:
		print("SKIP: none of the levels this checks is installed")
	elif _failures > 0:
		print("** FAILED ** %d checks" % _failures)
	else:
		print("own lighting: all checks passed on %d levels" % checked)
	get_tree().quit(1 if _failures > 0 else 0)
