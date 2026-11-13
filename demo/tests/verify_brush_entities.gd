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
	var fan_box := AABB(Vector3(151.9, -0.5, -0.5), Vector3(1, 1, 1))
	var slow_box := AABB(Vector3(180, 0, 0), Vector3(1, 1, 1))
	var swing_box := AABB(Vector3(190, 0, 0), Vector3(1, 2, 1))
	var tram_box := AABB(Vector3(160, 0, 0), Vector3(2, 1, 2))
	var flag_box := AABB(Vector3(170, 0, 0), Vector3(1, 2, 1))

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
	# Built around its origin brush, so its node stands at the origin, as the importer leaves
	# such a brush: the level's (0, -6000, 0) is Godot's (152.4, 0, 0).
	var fan := _brush(14, fan_box)
	fan.position = Vector3(152.4, 0, 0)
	var slow_fan := _brush(15, slow_box)
	var swing := _brush(16, swing_box)
	var tram := _brush(17, tram_box)
	_brush(18, flag_box)
	# The middle of the tram's box, and five metres above it: the two ends of its run.
	var low := tram_box.get_center()
	var high := low + Vector3(0, 5, 0)

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
		# Starts by itself after a second and a half, at full speed: 90 degrees a second.
		_entity("func_rotating", 14, fan_box, {"targetname": "fan", "origin": "0 -6000 0",
			"speed": "90", "spawnflags": "1"}),
		# Speeds up gradually: a tenth of its speed every tenth of a second.
		_entity("func_rotating", 15, slow_box, {"targetname": "slow_fan", "speed": "100",
			"fanfriction": "10", "spawnflags": "16"}),
		_entity("func_pendulum", 16, swing_box, {"targetname": "swing", "speed": "100",
			"distance": "90"}),
		_entity("func_train", 17, tram_box, {"targetname": "tram", "target": "bottom", "speed": "100"}),
		{"classname": "path_corner", "targetname": "bottom", "target": "top", "wait": "2",
			"position": low},
		# Faster on the way down, and it sets something off the first time it arrives.
		{"classname": "path_corner", "targetname": "top", "target": "bottom", "wait": "1",
			"speed": "200", "message": "flag", "spawnflags": "4", "position": high},
		_entity("func_wall_toggle", 18, flag_box, {"targetname": "flag"}),
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

	# -- something that spins --
	var fan_pivot := fan.get_parent() as Node3D
	_check("a spinning brush hangs under a pivot where it turns", fan_pivot != _map
		and fan_pivot.position.is_equal_approx(Vector3(152.4, 0, 0)))
	_check("hanging it there moves nothing (%s)" % str(fan.global_position),
		fan.global_position.is_equal_approx(Vector3(152.4, 0, 0)))
	# The checks above have long used up the second and a half it waits for the game to start.
	_check("a fan that starts by itself is going by now", _brushes.state_of("fan") == "spinning")
	var was := fan_pivot.transform.basis
	_run(0.5)
	var yaw := rad_to_deg((was.inverse() * fan_pivot.transform.basis).get_euler().y)
	_check("about the level's up, at its own speed (%.1f of 45 degrees in half a second)" % yaw,
		absf(absf(yaw) - 45.0) < 2.0)
	_check("about its origin brush, so it stays where it is (%s)" % str(fan.global_position),
		fan.global_position.is_equal_approx(Vector3(152.4, 0, 0)))
	_brushes.fire("fan")
	_run(0.5)
	_check("switched off, it runs down and stops", _brushes.state_of("fan") == "stopped")

	var slow_pivot := slow_fan.get_parent() as Node3D
	_brushes.fire("slow_fan")
	_run(0.5)
	yaw = rad_to_deg(slow_pivot.transform.basis.get_euler().y)
	# Half a second at full speed would be 50 degrees; winding up from rest it is about 12.
	_check("one built to wind up does so gradually (%.1f degrees in half a second)" % yaw,
		absf(yaw) > 5.0 and absf(yaw) < 25.0)

	# -- something that swings --
	var swing_pivot := swing.get_parent() as Node3D
	_check("a pendulum hangs still until it is told", _brushes.state_of("swing") == "still")
	_brushes.fire("swing")
	var widest := 0.0
	for i in 180:
		_brushes.step(STEP)
		widest = maxf(widest, rad_to_deg(swing_pivot.transform.basis.get_euler().y))
	_check("told, it swings out to the far end of its arc (%.1f of 90 degrees)" % widest,
		widest > 80.0 and widest < 95.0)
	_run(2.6)
	var back := rad_to_deg(swing_pivot.transform.basis.get_euler().y)
	_check("and back to where it was built (%.1f degrees)" % back, absf(back) < 15.0)
	_brushes.fire("swing")
	_check("told again, it stops", _brushes.state_of("swing") == "still")

	# -- a train --
	_check("a train starts with the middle of its box on its first corner (%s)" % str(tram.position),
		tram.position.is_equal_approx(Vector3.ZERO))
	_run(1.0)
	_check("one with a name waits to be told", tram.position.is_equal_approx(Vector3.ZERO))
	_brushes.fire("tram")
	_run(1.0)
	_check("told, it heads for the next corner (%s)" % str(tram.position),
		tram.position.y > 2.0 and tram.position.y < 3.0)
	_brushes.fire("tram")
	var stopped_at := tram.position
	_run(0.5)
	_check("told again, it stops where it is", tram.position.is_equal_approx(stopped_at))
	_brushes.fire("tram")
	_run(1.2)
	_check("and then carries on to that corner (%s)" % str(tram.position),
		tram.position.is_equal_approx(Vector3(0, 5, 0)))
	_check("which sets off what the corner names", _brushes.state_of("flag") == "off")
	# A second's wait, then half a second down: at 200 units a second it is below 3 m, at 100
	# it would not be.
	_run(1.5)
	_check("it leaves at the speed that corner gives (%s)" % str(tram.position),
		tram.position.y < 3.0)
	# Down, two seconds at the bottom, and up again, still at 200.
	_run(3.7)
	_check("round again to the top (%s)" % str(tram.position), tram.position.is_equal_approx(Vector3(0, 5, 0)))
	_check("where a corner that fires once does not fire twice", _brushes.state_of("flag") == "off")

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

	# Half-Life's own disposal: two pistons on path_corners, a turning beam of light, chains.
	var there: Dictionary = vfs.find_files("maps/disposal.bsp", PackedStringArray(["bsp"]),
		PackedStringArray(), PackedStringArray(), 1)
	var maps: PackedStringArray = there["files"]
	if maps.is_empty():
		print("  disposal is not there")
		return
	var level: Node3D = importer.load_map(str(maps[0]))
	_check("disposal loads", level != null)
	if level == null:
		return
	add_child(level)
	var placed: Array = importer.load_map_entities(str(maps[0]))
	var keeper := Node3D.new()
	keeper.set_script(preload("res://tests/brush_host.gd"))
	keeper.set("vfs", vfs)
	keeper.set("importer", importer)
	add_child(keeper)
	var moving: Node = BrushEntities.new()
	keeper.add_child(moving)
	moving.set_physics_process(false)
	moving.setup(keeper, level, placed)
	var has: Dictionary = moving.summary()
	_check("disposal: its fan, chains and pistons are taken up (%s spinning, %s swinging, %s trains, %s corners)"
		% [has["spinners"], has["pendulums"], has["trains"], has["corners"]],
		has["spinners"] >= 1 and has["pendulums"] == 7 and has["trains"] == 2 and has["corners"] >= 4)
	var piston := level.find_child("brush_161", true, false) as Node3D
	var beam := level.find_child("brush_141", true, false) as Node3D
	_check("disposal: the piston and the beam are there", piston != null and beam != null)
	if piston == null or beam == null:
		return
	var piston_was := piston.global_position
	var beam_was := beam.global_transform.basis
	var furthest := 0.0
	for i in 600:
		moving.step(STEP)
		furthest = maxf(furthest, piston.global_position.distance_to(piston_was))
	_check("disposal: a piston with no name sets off by itself (%.2f m from where it began)" % furthest,
		furthest > 0.5)
	_check("disposal: the beam of light turns", not beam.global_transform.basis.is_equal_approx(beam_was))
	keeper.queue_free()
	level.queue_free()
