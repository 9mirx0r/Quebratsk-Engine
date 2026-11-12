extends Node3D

## Is what a map collides with the same thing the map says is solid?
##
## The importer turns a GoldSrc map's trees into convex blocks. This suite does not trust
## that. For every map installed it reads the file again, by itself, in the plainest way
## there is (walk the tree from the top with one point until a leaf says what is there), and
## asks the physics engine the same question about the same point. The two answers have to
## agree, for hundreds of points per map, in both trees:
##
##   - the world at its real size (the "StaticBody3D" everything collides with), and
##   - the world as a standing player meets it (the "PlayerClip" body, which is where the
##     invisible walls are).
##
## Nothing here shares code with the importer, so the same mistake would have to be made
## twice, in two languages, to go unseen.
##
## It also times every load, since building thousands of blocks is not free.
##
## Loading a map takes about three seconds, nearly all of it textures, so by default this
## takes a sample spread over the whole list. QUEBRATSK_SOLIDS_MAPS sets how many, and
## `all` runs every map installed (about twenty minutes for 397).

const GamePaths := preload("res://tests/game_paths.gd")

const POINTS_PER_MAP := 200
const DEFAULT_MAPS := 40
## Map units. A point nearer than this to a plane it was tested against is too close to a
## boundary for two different calculations to be required to agree on, and is not counted.
const CLEARANCE := 2.0
const UNITS_TO_METRES := 0.0254
const PLAYER_CLIP_LAYER := 1 << 19

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

	print("\n=== MAP SOLIDS ===")
	var root := GamePaths.common("Half-Life")
	if not GamePaths.installed(root):
		print("SKIP: Half-Life is not installed (looked in %s)" % root)
		get_tree().quit()
		return
	_vfs.mount_directory("hl", root)

	var hit: Dictionary = _vfs.find_files("", PackedStringArray(["bsp"]),
		PackedStringArray(), PackedStringArray(), 0)
	var maps: Array = []
	for entry in hit.get("files", []):
		maps.append(str(entry))
	maps.sort()
	var wanted := OS.get_environment("QUEBRATSK_SOLIDS_MAPS")
	var limit := 0 if wanted == "all" else (int(wanted) if int(wanted) > 0 else DEFAULT_MAPS)
	print("%d map(s) installed, %s" % [maps.size(),
		"all compared" if limit == 0 or limit >= maps.size() else "%d compared" % limit])
	if limit > 0 and limit < maps.size():
		var step := maps.size() / float(limit)
		var picked: Array = []
		for i in limit:
			picked.append(maps[int(i * step)])
		maps = picked

	var rng := RandomNumberGenerator.new()
	rng.seed = 30
	var checked := 0
	var points_world := 0
	var points_player := 0
	var no_player_tree := 0
	var times: Array = []   # [msec, name, blocks]
	for uri in maps:
		var data: PackedByteArray = _vfs.read_file(uri)
		if data.size() < 124 or data.decode_s32(0) != 30:
			continue
		var label: String = uri.trim_prefix("vfs://hl/")
		var tree := _read_tree(data)
		if tree.is_empty():
			_fail("%s: no tree to compare with" % label)
			continue

		var started := Time.get_ticks_msec()
		var map: Node3D = _importer.load_map(uri)
		var took := Time.get_ticks_msec() - started
		if map == null:
			_fail("%s would not load" % label)
			continue

		# Brush entities sit in the world's empty space with solids of their own; only the
		# world is compared here.
		for child in map.get_children():
			if str(child.name).begins_with("brush_"):
				map.remove_child(child)
				child.free()
		var world: StaticBody3D = map.get_node_or_null("StaticBody3D")
		var player: StaticBody3D = map.get_node_or_null("PlayerClip")
		var blocks := 0 if world == null else world.get_child_count()
		var player_blocks := 0 if player == null else player.get_child_count()
		times.append([took, label, blocks + player_blocks])
		if world == null:
			_fail("%s: nothing solid" % label)
			map.free()
			continue

		add_child(map)
		await get_tree().physics_frame
		await get_tree().physics_frame

		var result := _compare(tree, rng, 1, false)
		points_world += result[0]
		if result[1] > 0:
			_fail("%s: %d of %d points disagree about what is solid (first: %s)"
				% [label, result[1], result[0], result[2]])
		if player == null or tree["clip_head"] < 0:
			no_player_tree += 1
		else:
			result = _compare(tree, rng, PLAYER_CLIP_LAYER, true)
			points_player += result[0]
			if result[1] > 0:
				_fail("%s: %d of %d points disagree about what stops a player (first: %s)"
					% [label, result[1], result[0], result[2]])

		checked += 1
		remove_child(map)
		map.free()
		if checked % 50 == 0:
			print("   ... %d maps" % checked)

	print("\n%d map(s) compared: %d points against the world, %d against the player's world"
		% [checked, points_world, points_player])
	if no_player_tree > 0:
		print("%d map(s) had no player tree to compare" % no_player_tree)
	if checked == 0:
		_fail("no map was compared")

	times.sort_custom(func(a, b): return a[0] > b[0])
	var total := 0
	for row in times:
		total += row[0]
	if not times.is_empty():
		print("\nLoad times: %.1f s in all, %d ms on average, median %d ms"
			% [total / 1000.0, total / times.size(), times[times.size() / 2][0]])
		for i in mini(5, times.size()):
			print("   %6d ms  %-40s %5d blocks" % [times[i][0], times[i][1], times[i][2]])

	print("\n=== SUMMARY ===")
	print("all checks passed" if _failures == 0 else "%d check(s) failed" % _failures)
	get_tree().quit(1 if _failures > 0 else 0)


## The parts of the file needed to ask "what is at this point", or {} if they are not there.
func _read_tree(data: PackedByteArray) -> Dictionary:
	var lumps: Array = []
	for i in 15:
		var at := data.decode_s32(4 + i * 8)
		var size := data.decode_s32(8 + i * 8)
		if at < 0 or size < 0 or at + size > data.size():
			return {}
		lumps.append([at, size])
	# Blue Shift keeps its planes in lump 0 and its entities in lump 1; everything else has
	# them the other way round. Entity text starts with a brace.
	var first := data.slice(lumps[0][0], lumps[0][0] + mini(lumps[0][1], 64)).get_string_from_ascii()
	var planes_lump := 1 if first.strip_edges().begins_with("{") else 0
	if lumps[14][1] < 64 or lumps[5][1] < 24 or lumps[10][1] < 28:
		return {}
	var model: int = lumps[14][0]
	return {
		"data": data,
		"planes": lumps[planes_lump][0], "plane_count": lumps[planes_lump][1] / 20,
		"nodes": lumps[5][0], "node_count": lumps[5][1] / 24,
		"leaves": lumps[10][0], "leaf_count": lumps[10][1] / 28,
		"clip": lumps[9][0], "clip_count": lumps[9][1] / 8,
		"mins": Vector3(data.decode_float(model), data.decode_float(model + 4), data.decode_float(model + 8)),
		"maxs": Vector3(data.decode_float(model + 12), data.decode_float(model + 16), data.decode_float(model + 20)),
		"head": data.decode_s32(model + 36),
		"clip_head": data.decode_s32(model + 40) if lumps[9][1] >= 8 else -1,
	}


## What the file says is at `p` (map units): [solid, how far the nearest plane tested was].
## Walks the drawn tree, or the standing player's when `player`.
func _contents_at(tree: Dictionary, p: Vector3, player: bool) -> Array:
	var data: PackedByteArray = tree["data"]
	var node: int = tree["clip_head"] if player else tree["head"]
	var count: int = tree["clip_count"] if player else tree["node_count"]
	var base: int = tree["clip"] if player else tree["nodes"]
	var stride := 8 if player else 24
	var nearest := INF
	var steps := 0
	while node >= 0:
		steps += 1
		if node >= count or steps > 100000:
			return [false, 0.0]   # a broken tree: do not count the point
		var at := base + node * stride
		var plane := data.decode_s32(at)
		if plane < 0 or plane >= tree["plane_count"]:
			return [false, 0.0]
		var pa: int = tree["planes"] + plane * 20
		var d := data.decode_float(pa) * p.x + data.decode_float(pa + 4) * p.y \
			+ data.decode_float(pa + 8) * p.z - data.decode_float(pa + 12)
		nearest = minf(nearest, absf(d))
		node = data.decode_s16(at + 4) if d >= 0.0 else data.decode_s16(at + 6)
	var contents := node
	if not player:
		var leaf := -1 - node
		if leaf >= tree["leaf_count"]:
			return [false, 0.0]
		contents = data.decode_s32(tree["leaves"] + leaf * 28)
	# Solid, or sky, which the importer deliberately makes solid too.
	return [contents == -2 or contents == -6, nearest]


## Ask both sides about random points inside the world's box.
## Returns [points counted, disagreements, a description of the first one].
func _compare(tree: Dictionary, rng: RandomNumberGenerator, mask: int, player: bool) -> Array:
	var space := get_world_3d().direct_space_state
	var mins: Vector3 = tree["mins"]
	var maxs: Vector3 = tree["maxs"]
	var counted := 0
	var wrong := 0
	var first := ""
	var query := PhysicsPointQueryParameters3D.new()
	query.collision_mask = mask
	for i in POINTS_PER_MAP:
		var p := Vector3(rng.randf_range(mins.x, maxs.x), rng.randf_range(mins.y, maxs.y),
			rng.randf_range(mins.z, maxs.z))
		var said := _contents_at(tree, p, player)
		if said[1] < CLEARANCE:
			continue
		counted += 1
		# The importer's change of axes and units, written out again.
		query.position = Vector3(-p.y, p.z, -p.x) * UNITS_TO_METRES
		var solid := not space.intersect_point(query, 1).is_empty()
		if solid != said[0]:
			wrong += 1
			if first.is_empty():
				first = "%s is %s in the file, %s in Godot" % [str(p.round()),
					"solid" if said[0] else "open", "solid" if solid else "open"]
	return [counted, wrong, first]
