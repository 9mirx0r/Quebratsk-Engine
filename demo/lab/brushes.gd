extends Node

## The parts of a level that are not scenery.
##
## Half of what a GoldSrc map contains is a brush entity: a door, a lift, a decoration you walk
## through, a wall you can break. They arrive as `brush_1`, `brush_2` … under the map, each
## with its own mesh and collider, and what each one should *do* is written in its classname.
##
## Everything here is the game's own behaviour, out of the Half-Life SDK's doors.cpp and the
## FGD the level editors used. Nothing is invented; where a number is a choice it says so.

const EntityCatalogue := preload("res://addons/quebratsk_editor/entity_catalogue.gd")
const SoundLoader := preload("res://addons/quebratsk_editor/sound_loader.gd")

const UNIT := 0.0254

## A door reports how it moves by a number, not a filename. The engine keeps a table and the
## map writes an index into it, so `movesnd 2` is doors/doormove2.wav. From doors.cpp.
const MOVE_SOUNDS := "doors/doormove%d.wav"
const STOP_SOUNDS := "doors/doorstop%d.wav"

## What a button, a lever or a latch sounds like, by the number a map writes.
##
## From ButtonSound() in buttons.cpp. Doors use this table too: a func_door's `locked_sound`
## is a button sound and not a door one, which is why the two func_door_rotating on cs_siege
## came back silent when they were looked for among doors/doormove*.
const BUTTON_SOUNDS := {
	1: "buttons/button1.wav", 2: "buttons/button2.wav", 3: "buttons/button3.wav",
	4: "buttons/button4.wav", 5: "buttons/button5.wav", 6: "buttons/button6.wav",
	7: "buttons/button7.wav", 8: "buttons/button8.wav", 9: "buttons/button9.wav",
	10: "buttons/button10.wav", 11: "buttons/button11.wav",
	12: "buttons/latchlocked1.wav", 13: "buttons/latchunlocked1.wav",
	14: "buttons/lightswitch2.wav",
	21: "buttons/lever1.wav", 22: "buttons/lever2.wav", 23: "buttons/lever3.wav",
	24: "buttons/lever4.wav", 25: "buttons/lever5.wav",
}

## SF_DOOR_USE_ONLY. Without it a func_door opens when you walk into it, which is why nearly
## every door in Counter-Strike has no button.
const USE_ONLY := 256

var _lab: Node3D
var _doors: Array = []
var _buttons: Array = []


func setup(lab: Node3D, map: Node3D, entities: Array) -> void:
	_lab = lab
	var passable := 0

	for e in entities:
		var entity: Dictionary = e
		var model := str(entity.get("model", ""))
		if not model.begins_with("*"):
			continue
		var node := map.get_node_or_null("brush_%s" % model.substr(1)) as MeshInstance3D
		if node == null:
			continue

		var cls := str(entity.get("classname", ""))
		match cls:
			"func_illusionary":
				# Scenery you walk straight through: a vine, an overhang, a bar of light. The
				# game never collides with these and cs_siege has seventy-three, every one of
				# which was an invisible wall here.
				_make_passable(node)
				passable += 1
			"func_door", "func_door_rotating":
				_build_door(node, entity, cls == "func_door_rotating")
			"func_button":
				# Pressed by walking into it when SF_BUTTON_TOUCH_ONLY is set, which is what
				# all three of cs_siege's are: spawnflags 1. Each fires a `target`, and on
				# that map all three call the same lift.
				_buttons.append({
					"node": node,
					"target": str(entity.get("target", "")),
					"sound": _named_sound(BUTTON_SOUNDS.get(int(entity.get("sounds", 0)), "")),
					"wait": maxf(float(entity.get("wait", 1.0)), 0.5),
					"delay": float(entity.get("delay", 0.0)),
					"reach": maxf((entity.get("bounds", AABB()) as AABB).size.length() * 0.5,
						1.0) + 1.0,
					"ready": 0.0,
					"audio": null,
				})
			"func_water":
				# The surface is drawn separately and prettier; this is the brush behind it,
				# which you swim through rather than walk into.
				_make_passable(node)

	if passable > 0:
		print("[brushes] %d brush(es) made passable" % passable)
	if not _doors.is_empty():
		var voiced := 0
		for d in _doors:
			if (d as Dictionary)["move_sound"] != null: voiced += 1
		print("[brushes] %d door(s), %d with a sound" % [_doors.size(), voiced])
	if not _buttons.is_empty():
		print("[brushes] %d button(s)" % _buttons.size())


## Take the collider off, leaving what you can see.
func _make_passable(node: MeshInstance3D) -> void:
	for child in node.get_children():
		if child is StaticBody3D:
			node.remove_child(child)
			child.queue_free()


## A door: where it goes, how fast, how long it waits, and what it sounds like.
##
## A sliding func_door moves along the direction its `angles` point, by its own size in that
## direction less `lip` — the overlap the mapper leaves behind so the doorway is not left with
## a hole in the frame. speed is units per second and wait is how long it stays open, with -1
## meaning it stays.
func _build_door(node: MeshInstance3D, entity: Dictionary, rotating: bool) -> void:
	var angles := _angles_of(entity)
	var travel := Vector3.ZERO

	if not rotating:
		# In Valve's frame the movement direction comes from pitch and yaw; through the axis
		# change it is the same direction expressed in Godot's.
		var yaw := deg_to_rad(angles.y)
		var pitch := deg_to_rad(angles.x)
		var valve := Vector3(cos(yaw) * cos(pitch), sin(yaw) * cos(pitch), -sin(pitch))
		var dir := Vector3(-valve.y, valve.z, -valve.x).normalized()

		var size: Vector3 = (entity.get("bounds", AABB()) as AABB).size
		var span := absf(size.x * dir.x) + absf(size.y * dir.y) + absf(size.z * dir.z)
		var lip := float(entity.get("lip", 0.0)) * UNIT
		travel = dir * maxf(span - lip, 0.05)

	_doors.append({
		"node": node,
		"shut": node.position,
		"open": node.position + travel,
		"rotating": rotating,
		"turn": deg_to_rad(90.0),   # func_door_rotating's default swing
		"speed": maxf(float(entity.get("speed", 100.0)) * UNIT, 0.2),
		"wait": float(entity.get("wait", 4.0)),
		"use_only": (int(entity.get("spawnflags", 0)) & USE_ONLY) != 0,
		"reach": maxf((entity.get("bounds", AABB()) as AABB).size.length() * 0.5, 1.5) + 1.2,
		"state": "shut",
		"at": 0.0,
		"hold": 0.0,
		# A rotating door declares neither movesnd nor stopsnd; what it has is locked_sound,
		# which is a button sound. Falling back to it is what gives those two a voice.
		"move_sound": _door_sound(MOVE_SOUNDS, int(entity.get("movesnd", 0))) 			if int(entity.get("movesnd", 0)) > 0 			else _named_sound(BUTTON_SOUNDS.get(int(entity.get("locked_sound", 0)), "")),
		"stop_sound": _door_sound(STOP_SOUNDS, int(entity.get("stopsnd", 0))),
		"audio": null,
	})


func _door_sound(pattern: String, index: int) -> AudioStream:
	if index <= 0:
		return null
	return _named_sound(pattern % index)


func _named_sound(named: String) -> AudioStream:
	if named.is_empty() or _lab == null:
		return null
	var hit: Dictionary = _lab.vfs.find_files("sound/" + named,
		PackedStringArray(["wav"]), PackedStringArray(), PackedStringArray(), 1)
	var files: PackedStringArray = hit["files"]
	if files.is_empty():
		return null
	return SoundLoader.load_sound(_lab.vfs, str(files[0]))


func _angles_of(entity: Dictionary) -> Vector3:
	var raw = entity.get("angles", "")
	if raw is Vector3:
		return raw as Vector3
	var parts := str(raw).split(" ", false)
	if parts.size() < 3:
		return Vector3.ZERO
	return Vector3(float(parts[0]), float(parts[1]), float(parts[2]))


func _physics_process(delta: float) -> void:
	var who: Node3D = _lab.get("_player") if _lab != null else null
	if who == null or not is_instance_valid(who):
		return

	var now := Time.get_ticks_msec() / 1000.0
	for bt in _buttons:
		var button: Dictionary = bt
		var pressed: MeshInstance3D = button["node"]
		if not is_instance_valid(pressed) or now < float(button["ready"]):
			continue
		if who.global_position.distance_to(pressed.global_position) >= float(button["reach"]):
			continue
		button["ready"] = now + float(button["wait"])
		_play(button, button["sound"], pressed)
		# What it targets. Doors are all this map's buttons call, and a door that has been
		# told to open stays open: that is what `wait -1` on the lift means.
		if not str(button["target"]).is_empty():
			for d2 in _doors:
				var aimed: Dictionary = d2
				if aimed["state"] == "shut":
					aimed["state"] = "opening"
					_play(aimed, aimed["move_sound"], aimed["node"])

	for d in _doors:
		var door: Dictionary = d
		var node: MeshInstance3D = door["node"]
		if not is_instance_valid(node):
			continue

		# Touch opens it, which is what a func_door without SF_USE_ONLY does. Measured against
		# the brush's own size rather than a fixed distance, because a garage door and a
		# cupboard are not approached from the same range.
		var near := who.global_position.distance_to(node.global_position) < float(door["reach"])
		if door["state"] == "shut" and near:
			door["state"] = "opening"
			_play(door, door["move_sound"], node)
		elif door["state"] == "open":
			door["hold"] -= delta
			# wait -1 means it stays open, which is how a lift's landing door behaves.
			if float(door["wait"]) >= 0.0 and door["hold"] <= 0.0 and not near:
				door["state"] = "closing"
				_play(door, door["move_sound"], node)

		var step: float = float(door["speed"]) * delta
		if door["state"] == "opening" or door["state"] == "closing":
			var span: float = (door["open"] as Vector3).distance_to(door["shut"] as Vector3)
			var per: float = step / maxf(span, 0.01) if not door["rotating"] else step
			door["at"] = clampf(float(door["at"]) + (per if door["state"] == "opening" else -per),
				0.0, 1.0)

			if door["rotating"]:
				node.rotation.y = float(door["at"]) * float(door["turn"])
			else:
				node.position = (door["shut"] as Vector3).lerp(door["open"] as Vector3,
					float(door["at"]))

			if door["state"] == "opening" and float(door["at"]) >= 1.0:
				door["state"] = "open"
				door["hold"] = float(door["wait"])
				_play(door, door["stop_sound"], node)
			elif door["state"] == "closing" and float(door["at"]) <= 0.0:
				door["state"] = "shut"
				_play(door, door["stop_sound"], node)


func _play(door: Dictionary, stream: AudioStream, node: Node3D) -> void:
	if stream == null:
		return
	if door["audio"] == null:
		var made := AudioStreamPlayer3D.new()
		made.unit_size = 6.0
		node.add_child(made)
		door["audio"] = made
		if _lab != null and _lab.has_method("track_sound"):
			_lab.track_sound(made)
	var audio: AudioStreamPlayer3D = door["audio"]
	audio.stream = stream
	audio.play()
