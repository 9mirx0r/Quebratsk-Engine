extends Node3D

const GamePaths := preload("res://tests/game_paths.gd")
const BrushEntities := preload("res://addons/quebratsk_editor/brush_entities.gd")

## Do the parts of a level that move, move — and for the right reason?
##
## A small level is made up by hand here, one thing of each kind, and somebody is walked
## through it: a door that opens when you come near, one that only its button opens, a
## trigger that sets off a chain, a lift. Then a real level is loaded to see that what it
## declares is found. Time is stepped by hand so the same thing happens on every run.

const STEP := 1.0 / 60.0

var _failures := 0
var _map: Node3D
var _walker: Node3D
var _brushes: Node


func _check(what: String, ok: bool) -> void:
	print("  %s  %s" % ["ok   " if ok else "WRONG", what])
	if not ok:
		_failures += 1


## One brush, the way the importer leaves it: a mesh under the map with its collider.
func _brush(index: int, box: AABB) -> MeshInstance3D:
	var node := MeshInstance3D.new()
	node.name = "brush_%d" % index
	var mesh := BoxMesh.new()
	mesh.size = box.size
	node.mesh = mesh
	var body := StaticBody3D.new()
	var shape := CollisionShape3D.new()
	var solid := BoxShape3D.new()
	solid.size = box.size
	shape.shape = solid
	body.add_child(shape)
	node.add_child(body)
	_map.add_child(node)
	return node


func _entity(cls: String, index: int, box: AABB, more: Dictionary = {}) -> Dictionary:
	var e := {"classname": cls, "model": "*%d" % index, "bounds": box}
	e.merge(more)
	return e


func _run(seconds: float) -> void:
	for i in int(round(seconds / STEP)):
		_brushes.step(STEP)


func _has_collider(node: Node) -> bool:
	for child in node.get_children():
		if child is StaticBody3D:
			return true
	return false


func _ready() -> void:
	print("\n=== BRUSH ENTITIES ===")
	_made_up_level()
	_real_level()
	if _failures > 0:
		print("\n** FAILED ** %d checks" % _failures)
	else:
		print("\nverify_brush_entities: all checks passed")
	get_tree().quit(1 if _failures > 0 else 0)


func _made_up_level() -> void:
	print("\n-- a level made up by hand --")
	_map = Node3D.new()
	add_child(_map)
	_walker = Node3D.new()
	add_child(_walker)

	# Each thing ten metres from the next, so that standing at one touches no other.
	var door_box := AABB(Vector3(0, 0, 0), Vector3(1, 2, 1))
	var gate_box := AABB(Vector3(10, 0, 0), Vector3(1, 2, 1))
	var button_box := AABB(Vector3(20, 1, 0), Vector3(0.2, 0.2, 0.2))
	var loose_box := AABB(Vector3(30, 1, 0), Vector3(0.2, 0.2, 0.2))
	var trigger_box := AABB(Vector3(40, 0, 0), Vector3(2, 2, 2))
	var wall_box := AABB(Vector3(50, 0, 0), Vector3(1, 2, 1))
	var crate_box := AABB(Vector3(60, 0, 0), Vector3(1, 1, 1))
	var lift_box := AABB(Vector3(70, 0, 0), Vector3(2, 0.2, 2))
	var toggle_box := AABB(Vector3(80, 0, 0), Vector3(1, 2, 1))
	var open_box := AABB(Vector3(90, 0, 0), Vector3(1, 2, 1))
	var vine_box := AABB(Vector3(100, 0, 0), Vector3(1, 2, 1))
	var use_box := AABB(Vector3(110, 1, 0), Vector3(0.2, 0.2, 0.2))
	var lamp_box := AABB(Vector3(120, 0, 0), Vector3(1, 2, 1))

	var door := _brush(1, door_box)
	var gate := _brush(2, gate_box)
	_brush(3, button_box)
	_brush(4, loose_box)
	var trigger := _brush(5, trigger_box)
	var wall := _brush(6, wall_box)
	var crate := _brush(7, crate_box)
	var lift := _brush(8, lift_box)
	_brush(9, toggle_box)
	var open_door := _brush(10, open_box)
	var vine := _brush(11, vine_box)
	_brush(12, use_box)
	_brush(13, lamp_box)

	var entities := [
		{"classname": "worldspawn"},
		_entity("func_door", 1, door_box, {"angles": "0 0 0", "speed": "100", "wait": "1"}),
		_entity("func_door", 2, gate_box, {"targetname": "gate", "angles": "0 0 0",
			"speed": "100", "wait": "-1", "target": "lamp"}),
		_entity("func_button", 3, button_box, {"target": "gate", "spawnflags": "256", "wait": "1"}),
		_entity("func_button", 4, loose_box, {"target": "nobody_home", "spawnflags": "256"}),
		_entity("trigger_once", 5, trigger_box, {"target": "chain"}),
		{"classname": "multi_manager", "targetname": "chain", "origin": "0 0 0",
			"position": Vector3.ZERO, "wall": "0", "relay": "2"},
		_entity("func_wall_toggle", 6, wall_box, {"targetname": "wall"}),
		{"classname": "trigger_relay", "targetname": "relay", "target": "crate"},
		_entity("func_breakable", 7, crate_box, {"targetname": "crate", "health": "50", "material": "1"}),
		_entity("func_plat", 8, lift_box, {"height": "100", "speed": "100"}),
		_entity("func_door", 9, toggle_box, {"targetname": "toggle", "angles": "0 0 0",
			"speed": "100", "spawnflags": "32"}),
		_entity("func_door", 10, open_box, {"angles": "0 0 0", "speed": "100", "spawnflags": "1"}),
		_entity("func_illusionary", 11, vine_box),
		_entity("func_button", 12, use_box, {"target": "toggle"}),
		_entity("func_wall_toggle", 13, lamp_box, {"targetname": "lamp", "spawnflags": "1"}),
		# A ladder has a box and no brush to show for it.
		{"classname": "func_ladder", "model": "*40", "bounds": AABB(Vector3(130, 0, 0), Vector3(0.5, 4, 0.5))},
		{"classname": "func_water", "model": "*41", "bounds": AABB(Vector3(140, 0, 0), Vector3(4, 1, 4))},
	]

	_brushes = BrushEntities.new()
	add_child(_brushes)
	# Time is stepped from here, not by the engine.
	_brushes.set_physics_process(false)
	_brushes.press_by_touch = false
	_brushes.setup(null, _map, entities)
	_brushes.actor = _walker

	var counts: Dictionary = _brushes.summary()
	_check("four doors, three buttons, a lift, a trigger and a breakable are found (%s)" % str(counts),
		counts["doors"] == 4 and counts["buttons"] == 3 and counts["lifts"] == 1
		and counts["triggers"] == 1 and counts["breakables"] == 1)
	_check("the ladder is found by its box alone", counts["ladders"] == 1)
	_check("scenery you walk through has no collider", not _has_collider(vine))
	_check("a trigger is neither seen nor walked into", not trigger.visible and not _has_collider(trigger))

	# Far from everything.
	_walker.global_position = Vector3(0, 1, 500)
	_run(1.0)
	_check("a door nobody is near stays shut", door.position.is_equal_approx(Vector3.ZERO))

	# -- a door that opens when you walk up to it --
	_walker.global_position = Vector3(0.5, 1, 1.3)
	_run(1.0)
	# Its `angles` point along the level's +x, which is -z here, and it is a metre deep.
	_check("walking up to a door opens it by its own depth (%s)" % str(door.position),
		door.position.is_equal_approx(Vector3(0, 0, -1)))
	_run(3.0)
	_check("it stays open while somebody stands in the way", door.position.is_equal_approx(Vector3(0, 0, -1)))
	_walker.global_position = Vector3(0, 1, 500)
	_run(3.0)
	_check("and shuts again once they have gone", door.position.is_equal_approx(Vector3.ZERO))

	# -- a door that is somebody's target --
	_walker.global_position = Vector3(10.5, 1, 1.3)
	_run(1.5)
	_check("a door with a name does not open by being walked into", gate.position.is_equal_approx(Vector3.ZERO))
	_check("the thing that door sets off is still waiting", _brushes.state_of("lamp") == "off")

	_walker.global_position = Vector3(20.1, 1.1, 0.5)
	_run(1.5)
	_check("its button opens it from ten metres off (%s)" % str(gate.position),
		gate.position.is_equal_approx(Vector3(0, 0, -1)))
	_check("the button opened that door and no other", door.position.is_equal_approx(Vector3.ZERO))
	_check("a door sets off its own target once it is open", _brushes.state_of("lamp") == "on")
	_walker.global_position = Vector3(0, 1, 500)
	_run(8.0)
	_check("a door that waits for ever stays open", _brushes.state_of("gate") == "open")

	# -- a button whose target nobody answers to --
	_walker.global_position = Vector3(30.1, 1.1, 0.5)
	_run(0.5)
	_check("a target nothing answers to is reported, not lost (%s)" % str(_brushes.unanswered()),
		_brushes.unanswered().has("nobody_home"))

	# -- a button that wants the use key --
	_walker.global_position = Vector3(110.1, 1.1, 0.8)
	_run(1.0)
	_check("a button that wants the use key ignores being walked into", _brushes.state_of("toggle") == "shut")
	_check("the use key does nothing when looking away from it",
		not _brushes.use(_walker.global_position, Vector3(0, 0, 1)))
	_check("the use key presses the button in front of you",
		_brushes.use(_walker.global_position, Vector3(0, 0, -1)))
	_run(1.5)
	_check("which opens the door it names", _brushes.state_of("toggle") == "open")
	_run(10.0)
	_check("a door told not to return stays open by itself", _brushes.state_of("toggle") == "open")
	_brushes.fire("toggle")
	_run(1.5)
	_check("and shuts when it is signalled again", _brushes.state_of("toggle") == "shut")

	# -- a door built open --
	# The game puts such a door at the far end of its travel and has it come back.
	_check("a door built open starts at the far end of its travel (%s)" % str(open_door.position),
		open_door.position.is_equal_approx(Vector3(0, 0, -1)))
	_walker.global_position = Vector3(90.5, 1, -0.3)
	_run(1.0)
	_check("and comes back to where the mapper drew it when used (%s)" % str(open_door.position),
		open_door.position.is_equal_approx(Vector3.ZERO))

	# -- a trigger, a multi_manager, a relay --
	_check("the wall is there to begin with", wall.visible and _brushes.state_of("wall") == "on")
	_walker.global_position = Vector3(41, 1, 1)
	_run(0.1)
	_check("walking into a trigger takes the wall away at once", not wall.visible and _brushes.state_of("wall") == "off")
	_check("the crate is still whole a second later", is_instance_valid(crate) and not crate.is_queued_for_deletion())
	_run(2.5)
	_check("and comes apart when its two seconds are up", not is_instance_valid(crate) or crate.is_queued_for_deletion())
	_run(3.0)
	_check("a trigger_once never fires again", _brushes.state_of("wall") == "off")

	# -- a lift --
	_check("a lift starts lowered by its height (%s)" % str(lift.position),
		lift.position.is_equal_approx(Vector3(0, -2.54, 0)))
	# Standing on it: its top is 0.2 above the box's floor, less the 2.54 it has dropped.
	_walker.global_position = Vector3(71, -2.3, 1)
	_run(0.5)
	_check("stepping on sets it rising", lift.position.y > -2.54 and lift.position.y < 0.0)
	_walker.global_position = Vector3(71, lift.position.y + 0.25, 1)
	_run(1.5)
	_check("it stops at the top", lift.position.is_equal_approx(Vector3.ZERO))
	_walker.global_position = Vector3(71, 0.25, 1)
	_run(6.0)
	_check("and waits there while somebody is on it", lift.position.is_equal_approx(Vector3.ZERO))
	_walker.global_position = Vector3(0, 1, 500)
	_run(6.0)
	_check("then goes back down by itself", lift.position.is_equal_approx(Vector3(0, -2.54, 0)))

	# -- shooting --
	_check("a shot at a wall that is not breakable is not claimed", not _brushes.hurt(wall, 100.0))

	_brushes.queue_free()
	_map.queue_free()


func _real_level() -> void:
	print("\n-- a real level --")
	var root := GamePaths.common("Half-Life/cstrike")
	if not GamePaths.installed(root):
		print("  Counter-Strike is not installed at %s: nothing to check" % root)
		return

	var vfs := VFSManager.new()
	var importer := UnifiedAssetImporter.new()
	add_child(vfs)
	add_child(importer)
	importer.set_vfs(vfs)
	var n := 0
	for game in [GamePaths.common("Half-Life/valve"), root]:
		var scan: Dictionary = vfs.scan_game_directory(game)
		for archive in scan.get("archives", []):
			vfs.mount_container("g%d" % n, str(archive))
			n += 1
		vfs.mount_directory("loose%d" % n, game)
		n += 1

	for named in ["cs_siege", "de_aztec", "cs_assault"]:
		var hit: Dictionary = vfs.find_files("maps/%s.bsp" % named, PackedStringArray(["bsp"]),
			PackedStringArray(), PackedStringArray(), 1)
		var files: PackedStringArray = hit["files"]
		if files.is_empty():
			print("  %s is not there" % named)
			continue
		var map: Node3D = importer.load_map(str(files[0]))
		_check("%s loads" % named, map != null)
		if map == null:
			continue
		add_child(map)
		var entities: Array = importer.load_map_entities(str(files[0]))

		# What the level itself declares, counted straight from its entities.
		var declared := {"doors": 0, "buttons": 0, "breakables": 0, "ladders": 0}
		for e in entities:
			var cls := str((e as Dictionary).get("classname", ""))
			var node_there := map.get_node_or_null("brush_%s" % str((e as Dictionary).get("model", "")).substr(1)) != null
			if (cls == "func_door" or cls == "func_door_rotating") and node_there: declared["doors"] += 1
			if cls == "func_button" and node_there: declared["buttons"] += 1
			if cls == "func_breakable" and node_there: declared["breakables"] += 1
			if cls == "func_ladder" and (e as Dictionary).has("bounds"): declared["ladders"] += 1

		var host := Node3D.new()
		host.set_script(preload("res://tests/brush_host.gd"))
		host.set("vfs", vfs)
		host.set("importer", importer)
		add_child(host)
		var brushes: Node = BrushEntities.new()
		host.add_child(brushes)
		brushes.set_physics_process(false)
		brushes.setup(host, map, entities)
		var found: Dictionary = brushes.summary()
		_check("%s: every door, button, breakable and ladder it declares is taken up (%s)" % [named, str(declared)],
			found["doors"] == declared["doors"] and found["buttons"] == declared["buttons"]
			and found["breakables"] == declared["breakables"] and found["ladders"] == declared["ladders"])

		# Ten seconds of the level with nobody in it: nothing should move or break.
		for i in 600:
			brushes.step(STEP)
		_check("%s: nothing fires with nobody there" % named, brushes.unanswered().is_empty())

		host.queue_free()
		map.queue_free()
