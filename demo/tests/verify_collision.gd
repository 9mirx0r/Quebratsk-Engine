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
