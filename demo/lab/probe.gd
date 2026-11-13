extends RefCounted

## Driving the lab from outside, for tests.
##
## A test that walks the player somewhere used to be written out in full every time: swap the
## level, put the player down, hold a key, measure. These are those four things once, so a
## test is a few lines and its answer is a handful of numbers rather than a screen of log.
##
## Nothing in the game calls this. It is loaded by whatever is doing the testing:
##
##     var Probe = load("res://lab/probe.gd")
##     await Probe.open(tree, "disposal")
##     return await Probe.walk(tree, {"from": Vector3(-9.75, -4.8, -52.6), "yaw": PI,
##             "keys": ["W"], "seconds": 3.0})

const LAB := "res://lab/lab.tscn"

const KEYS := {
	"W": KEY_W, "S": KEY_S, "A": KEY_A, "D": KEY_D, "SPACE": KEY_SPACE, "CTRL": KEY_CTRL,
	"SHIFT": KEY_SHIFT, "E": KEY_E, "R": KEY_R,
}

## Slower than this, in metres a second, counts as standing still.
const STALLED := 0.3


## Open a map by name, replacing whichever one is open, and wait until the player is in it.
## A launch cannot be given a map from outside, so the level is changed once it is running.
static func open(tree: SceneTree, map: String, patience := 40.0) -> Dictionary:
	var old := tree.root.get_node_or_null("Lab")
	if old != null:
		tree.root.remove_child(old)
		old.free()
	var lab: Node = (load(LAB) as PackedScene).instantiate()
	lab.set("map_name", map)
	lab.name = "Lab"
	tree.root.add_child(lab)
	tree.current_scene = lab

	var began := Time.get_ticks_msec()
	while player(tree) == null:
		if (Time.get_ticks_msec() - began) / 1000.0 > patience:
			return {"ok": false, "map": map, "why": "no player after %.0f s" % patience}
		await tree.process_frame
	# A few ticks more, so it has come to rest on the floor it was put above.
	for i in 30:
		await tree.physics_frame
	return {"ok": true, "map": map, "at": _round(player(tree).global_position),
			"seconds": snappedf((Time.get_ticks_msec() - began) / 1000.0, 0.1)}


static func player(tree: SceneTree) -> CharacterBody3D:
	return tree.root.get_node_or_null("Lab/Player") as CharacterBody3D


## Put the player somewhere, facing `yaw`, standing still.
static func place(tree: SceneTree, at: Vector3, yaw := 0.0) -> void:
	var body := player(tree)
	body.global_position = at
	body.rotation.y = yaw
	body.velocity = Vector3.ZERO


## Hold some keys for a while and say what happened.
##
## `how` takes "keys" (names from KEYS), "seconds", and optionally "from" and "yaw" to start
## from a known place. The keys are sent again on every tick: a key pressed once is forgotten
## the moment the window loses focus, and the player then walks nowhere for no visible reason.
static func walk(tree: SceneTree, how: Dictionary) -> Dictionary:
	var body := player(tree)
	if body == null:
		return {"ok": false, "why": "no player"}
	if how.has("from"):
		place(tree, how["from"], float(how.get("yaw", body.rotation.y)))
		for i in 20:
			await tree.physics_frame

	var keys: Array = how.get("keys", ["W"])
	var seconds := float(how.get("seconds", 2.0))
	var start := body.global_position
	var last := start
	var top := 0.0
	var slowest := INF
	var stalled := 0
	var longest_stall := 0
	var run := 0
	var ticks := 0
	var settle := int(0.4 * Engine.physics_ticks_per_second)
	var total := int(seconds * Engine.physics_ticks_per_second)

	while ticks < total:
		_send(keys, true)
		await tree.physics_frame
		ticks += 1
		var here := body.global_position
		var step := here - last
		step.y = 0.0
		var speed := step.length() * Engine.physics_ticks_per_second
		last = here
		top = maxf(top, speed)
		# Getting up to speed is not a stall.
		if ticks <= settle:
			continue
		slowest = minf(slowest, speed)
		if speed < STALLED:
			stalled += 1
			run += 1
			longest_stall = maxi(longest_stall, run)
		else:
			run = 0
	_send(keys, false)

	var moved := body.global_position - start
	var rate := float(Engine.physics_ticks_per_second)
	return {
		"ok": true,
		"from": _round(start), "to": _round(body.global_position),
		"went": snappedf(Vector2(moved.x, moved.z).length(), 0.01),
		"climbed": snappedf(moved.y, 0.01),
		"top_speed": snappedf(top, 0.01),
		"slowest": snappedf(slowest if slowest < INF else 0.0, 0.01),
		"stalled_seconds": snappedf(stalled / rate, 0.01),
		"longest_stall": snappedf(longest_stall / rate, 0.01),
		"on_floor": body.is_on_floor(),
	}


static func _send(keys: Array, down: bool) -> void:
	for name in keys:
		var code: int = KEYS.get(str(name).to_upper(), KEY_NONE)
		if code == KEY_NONE:
			continue
		var event := InputEventKey.new()
		event.keycode = code
		event.physical_keycode = code
		event.pressed = down
		Input.parse_input_event(event)


static func _round(v: Vector3) -> Vector3:
	return v.snappedf(0.01)
