extends Node3D

## Can you stand on what this imports?
##
## A CollisionShape3D that exists proves nothing: it can be empty, it can be the wrong
## shape, it can sit in the wrong place. The only evidence that counts is dropping a body
## onto the thing and watching it stop. So this runs real physics and reports where each
## body came to rest, or that it did not.

const HL2 := "C:/Program Files (x86)/Steam/steamapps/common/half-life 2/hl2"
const GMOD := "C:/Program Files (x86)/Steam/steamapps/common/GarrysMod/garrysmod"
## GoldSrc, whose maps are a different format entirely from the Source ones above.
const HL1 := "C:/Program Files (x86)/Steam/steamapps/common/Half-Life/valve"
const CS16 := "C:/Program Files (x86)/Steam/steamapps/common/Half-Life/cstrike"

## Physics ticks to run per drop. At 60 Hz this is two seconds, long enough for a fall
## from 20 m and for anything that is going to keep sliding to have kept sliding.
const TICKS := 120

var _vfs: VFSManager
var _importer: UnifiedAssetImporter


func _ready() -> void:
	_vfs = VFSManager.new()
	_importer = UnifiedAssetImporter.new()
	add_child(_vfs)
	add_child(_importer)
	_importer.set_vfs(_vfs)

	# Both halves of a game: the archives, and the loose tree beside them. Half-Life 2 keeps
	# its maps as plain .bsp files in hl2/maps/, so an archives-only mount finds no map at
	# all, which is exactly what this check needs.
	var g := 0
	for game in [HL2, GMOD, HL1, CS16]:
		var scan: Dictionary = _vfs.scan_game_directory(game)
		var n := 0
		for archive in scan.get("archives", []):
			_vfs.mount_container("g%d_%d" % [g, n], str(archive))
			n += 1
		_vfs.mount_directory("g%d_loose" % g, game)
		g += 1

	print("\n=== COLLISION VERIFICATION ===")
	await _check_map()
	await _check_prop()
	await _check_character()
	await _check_character_walks()
	print("\n=== DONE ===")
	get_tree().quit()


## Drop a rigid body from `height` above the origin and report where it settles.
## Returns the resting Y, or NAN if it never stopped.
func _drop_onto(target: Node3D, height: float) -> float:
	add_child(target)

	var body := RigidBody3D.new()
	var shape := CollisionShape3D.new()
	var sphere := SphereShape3D.new()
	sphere.radius = 0.25
	shape.shape = sphere
	body.add_child(shape)
	body.position = Vector3(0, height, 0)
	add_child(body)

	var last := body.position.y
	var still_for := 0
	for i in TICKS:
		await get_tree().physics_frame
		var moved: float = absf(body.position.y - last)
		last = body.position.y
		# Resting means it stopped falling, not that it stopped entirely: a ball can roll
		# along a floor for a long time and still be standing on something.
		still_for = still_for + 1 if moved < 0.001 else 0
		if still_for >= 10:
			break

	var resting := body.position.y if still_for >= 10 else NAN
	body.queue_free()
	target.queue_free()
	return resting


func _check_map() -> void:
	print("
1. A map you can walk on")
	var hit: Dictionary = _vfs.find_files("", PackedStringArray(["bsp"]),
		PackedStringArray(), PackedStringArray(), 0)
	var all: PackedStringArray = hit["files"]
	if all.is_empty():
		print("   no .bsp mounted")
		return

	# The version word after the magic says which engine wrote it. GoldSrc BSP30 has no
	# magic at all, just the version 30 in the first four bytes; Source's VBSP does.
	var by_kind := {}
	for entry in all:
		var head := _vfs.read_file(str(entry))
		if head.size() < 8:
			continue
		var magic := head.slice(0, 4).get_string_from_ascii()
		var key := "VBSP v%d" % head.decode_u32(4) if magic == "VBSP" else "raw v%d" % head.decode_u32(0)
		by_kind[key] = int(by_kind.get(key, 0)) + 1
	print("   %d maps mounted:" % all.size())
	for k in by_kind:
		print("      %-12s %4d" % [k, by_kind[k]])

	# Try each kind rather than whichever happens to sort first, so a format that works is
	# not hidden behind one that does not.
	var tried := {}
	var walkable := ""
	for entry in all:
		var uri := str(entry)
		var head := _vfs.read_file(uri)
		if head.size() < 8:
			continue
		var magic := head.slice(0, 4).get_string_from_ascii()
		var key := "VBSP v%d" % head.decode_u32(4) if magic == "VBSP" else "raw v%d" % head.decode_u32(0)
		if tried.has(key):
			continue
		tried[key] = true

		var map: Node3D = _importer.load_map(uri)
		if map == null:
			print("   %-12s %-28s -> nothing decoded" % [key, uri.get_file()])
			continue
		var body: StaticBody3D = map.get_node_or_null("StaticBody3D")
		var col: CollisionShape3D = null
		if body != null:
			col = body.get_node_or_null("CollisionShape3D")
		var shape: ConcavePolygonShape3D = col.shape if col != null else null
		var faces: int = 0 if shape == null else int(shape.get_faces().size() / 3.0)
		print("   %-12s %-28s -> %d collision triangles" % [key, uri.get_file(), faces])
		if faces > 0 and walkable.is_empty():
			walkable = uri
		map.queue_free()

	if walkable.is_empty():
		print("   nothing that can be stood on  ** FAILED **")
		return

	var map2: Node3D = _importer.load_map(walkable)
	var top: float = (map2 as MeshInstance3D).get_aabb().end.y + 5.0
	var rest := await _drop_onto(map2, top)
	if is_nan(rest):
		print("   %s: a ball dropped from %.1f m never came to rest  ** FELL THROUGH **"
			% [walkable.get_file(), top])
	else:
		print("   %s: a ball dropped from %.1f m came to rest at y=%.2f"
			% [walkable.get_file(), top, rest])


## Which collider each kind of model ends up with, over a sample rather than one file.
## A prop with no bones gets its own triangles; anything skinned gets a capsule, because a
## trimesh collider would be frozen in the rest pose while the mesh animates away from it.
func _check_prop() -> void:
	print("
2. What collider each model gets")
	var hit: Dictionary = _vfs.find_files("props", PackedStringArray(["mdl"]),
		PackedStringArray(["vvd", "vtx", "ani", "phy"]), PackedStringArray(), 25)

	var trimesh := 0
	var capsule := 0
	var none := 0
	var example := ""
	for entry in hit["files"]:
		var node: Node3D = _importer.load_model(str(entry))
		if node == null:
			continue
		var body: StaticBody3D = node.get_node_or_null("StaticBody3D")
		var col: CollisionShape3D = null
		if body != null:
			col = body.get_node_or_null("CollisionShape3D")
		var shape: Shape3D = col.shape if col != null else null
		if shape is ConcavePolygonShape3D:
			trimesh += 1
			if example.is_empty():
				var faces: int = int((shape as ConcavePolygonShape3D).get_faces().size() / 3.0)
				example = "%s: %d triangles" % [str(entry).get_file(), faces]
		elif shape is CapsuleShape3D:
			capsule += 1
		else:
			none += 1
			print("   %s got no collider  ** FAILED **" % str(entry).get_file())
		node.queue_free()

	print("   of %d props: %d trimesh, %d capsule, %d without"
		% [trimesh + capsule + none, trimesh, capsule, none])
	if not example.is_empty():
		print("   e.g. %s" % example)


func _check_character() -> void:
	print("\n3. A character you can bump into")
	var hit: Dictionary = _vfs.find_files("models/player/", PackedStringArray(["mdl"]),
		PackedStringArray(), PackedStringArray(), 6)
	for entry in hit["files"]:
		var node: Node3D = _importer.load_model(str(entry))
		if node == null:
			continue
		var body: StaticBody3D = node.get_node_or_null("StaticBody3D")
		if body == null:
			print("   %s -> no collider  ** FAILED **" % str(entry).get_file())
			node.queue_free()
			return
		var col: CollisionShape3D = body.get_node_or_null("CollisionShape3D")
		var caps: CapsuleShape3D = col.shape if col != null else null
		if caps == null:
			print("   %s -> collider carries no capsule  ** FAILED **" % str(entry).get_file())
		else:
			# A person-sized capsule is roughly 1.8 m tall and 0.3 m across. Numbers far
			# from that mean the bounds were read in the wrong units or the wrong space.
			print("   %s -> capsule %.2f m tall, radius %.2f m, centred at y=%.2f"
				% [str(entry).get_file(), caps.height, caps.radius, col.position.y])
		node.queue_free()
		return
	print("   no player model found")


## A character has to land on a map and then be able to move along it.
##
## A CharacterBody3D that exists proves neither. It can be built around no shape at all, or
## around a capsule left inside a static body it then collides with and cannot escape, which
## is the failure that looks most like success: the node is right, the collider is right,
## and it will not move a centimetre.
func _check_character_walks() -> void:
	print("
4. A character that lands on a map and walks along it")

	var maps: Dictionary = _vfs.find_files("as_oilrig", PackedStringArray(["bsp"]),
		PackedStringArray(), PackedStringArray(), 1)
	# Several candidates, not the first: a model whose .vvd is in an archive that is not
	# mounted fails to load for reasons that have nothing to do with what is being checked.
	var models: Dictionary = _vfs.find_files("models/player/", PackedStringArray(["mdl"]),
		PackedStringArray(["vvd", "vtx", "ani", "phy"]), PackedStringArray(), 12)
	if (maps["files"] as PackedStringArray).is_empty() or (models["files"] as PackedStringArray).is_empty():
		print("   needs one GoldSrc map and one player model; not both are here")
		return

	var map: Node3D = _importer.load_map(str(maps["files"][0]))
	if map == null:
		print("   the map did not load")
		return
	add_child(map)

	# A character is 1.79 m tall. If the map is not tens of metres across, the two were not
	# converted to the same units and nothing else in this check will make sense.
	var box: AABB = (map as MeshInstance3D).get_aabb()
	print("   map spans %.1f x %.1f x %.1f m, from y=%.1f to y=%.1f"
		% [box.size.x, box.size.y, box.size.z, box.position.y, box.end.y])

	var who: Node3D = null
	var chosen := ""
	for entry in models["files"]:
		who = _importer.load_character(str(entry))
		if who != null:
			chosen = str(entry)
			break
	if who == null:
		print("   no player model loaded at all, last error %d  ** FAILED **"
			% _importer.get_last_error_code())
		map.queue_free()
		return
	var body := who as CharacterBody3D
	if body == null:
		print("   got a %s, not a CharacterBody3D  ** FAILED **" % who.get_class())
		who.queue_free()
		map.queue_free()
		return

	var col: CollisionShape3D = body.get_node_or_null("CollisionShape3D")
	var skel: Skeleton3D = body.get_node_or_null("Skeleton3D")
	var trapped: bool = skel != null and skel.get_node_or_null("StaticBody3D") != null
	print("   %s -> CharacterBody3D, collider=%s, skeleton=%s, static body left inside=%s"
		% [chosen.get_file(),
		   "none" if col == null or col.shape == null else col.shape.get_class(),
		   "none" if skel == null else "%d bones" % skel.get_bone_count(),
		   "yes  ** it would fight itself **" if trapped else "no"])

	# Find a floor inside the level rather than dropping from above it. A GoldSrc BSP is a
	# sealed box: anything released over the top lands on the outer shell, stands on it
	# perfectly well, and is walled in on every side. Which is what was happening.
	add_child(body)
	var spawn := _floor_inside(box)
	if spawn == Vector3.INF:
		print("   found no floor inside the level to stand on")
		body.queue_free()
		map.queue_free()
		return
	body.position = spawn
	print("   standing inside the level at %s" % str(spawn.round()))

	var landed := false
	for i in 240:
		await get_tree().physics_frame
		body.velocity.y -= 9.8 * get_physics_process_delta_time()
		body.move_and_slide()
		if body.is_on_floor():
			landed = true
			break
	if not landed:
		print("   it never landed  ** FELL THROUGH **")
		body.queue_free()
		map.queue_free()
		return

	var landing := body.position
	print("   landed at y=%.2f" % landing.y)

	# Then try to walk it, in four directions rather than one. The origin of a map is not
	# an open field: being blocked heading north says nothing about whether the character can
	# move at all, which is the actual question.
	var best := 0.0
	var best_dir := ""
	for pair in [[Vector3(2, 0, 0), "+x"], [Vector3(-2, 0, 0), "-x"],
			[Vector3(0, 0, 2), "+z"], [Vector3(0, 0, -2), "-z"]]:
		body.position = landing
		body.velocity = Vector3.ZERO
		for i in 60:
			await get_tree().physics_frame
			var push: Vector3 = pair[0]
			body.velocity = Vector3(push.x, body.velocity.y - 9.8 * get_physics_process_delta_time(), push.z)
			body.move_and_slide()
		var moved: float = Vector2(body.position.x - landing.x, body.position.z - landing.z).length()
		if moved > best:
			best = moved
			best_dir = str(pair[1])

	print("   walked for one second: furthest %.2f m, heading %s%s"
		% [best, best_dir if best_dir != "" else "nowhere",
		   "" if best > 0.5 else "   ** it cannot move in any direction **"])
	if best <= 0.5:
		print("      on wall=%s, on floor=%s, %d contact(s)"
			% [body.is_on_wall(), body.is_on_floor(), body.get_slide_collision_count()])
	body.queue_free()
	map.queue_free()


## A point just above a floor somewhere inside the level.
##
## Casts downward from head height at a spread of positions across the map and takes the
## first that finds ground with room above it. Maps do not record where a player belongs in
## any form this importer reads yet, so this is how a spawn point gets chosen.
func _floor_inside(box: AABB) -> Vector3:
	var space := get_world_3d().direct_space_state
	var centre := box.position + box.size * 0.5

	for fx in [0.5, 0.4, 0.6, 0.3, 0.7]:
		for fz in [0.5, 0.4, 0.6, 0.3, 0.7]:
			var x: float = box.position.x + box.size.x * fx
			var z: float = box.position.z + box.size.z * fz
			var from := Vector3(x, box.end.y - 1.0, z)
			var to := Vector3(x, box.position.y, z)
			var query := PhysicsRayQueryParameters3D.create(from, to)
			var hit := space.intersect_ray(query)
			if hit.is_empty():
				continue
			var ground: Vector3 = hit["position"]
			# Room to stand: nothing within two metres above the ground we just found.
			var up := PhysicsRayQueryParameters3D.create(
				ground + Vector3(0, 0.2, 0), ground + Vector3(0, 2.2, 0))
			if not space.intersect_ray(up).is_empty():
				continue
			return ground + Vector3(0, 0.1, 0)
	return Vector3.INF
