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

## What a func_breakable is made of, by the number the map writes. The enum's order is the
## table: glass, wood, metal, flesh, cinder block, ceiling tile, computer, unbreakable glass,
## rocks, none.
##
## Each carries what it sounds like coming apart and what it leaves behind, out of
## CBreakable::Die. de_aztec's crates declare material 8 — they are rocks, not wood, which is
## the mapper being right about a stone temple rather than a mistake.
const MATERIALS := [
	{"name": "glass",    "sounds": ["debris/bustglass1.wav", "debris/bustglass2.wav"],
	 "gibs": "models/glassgibs.mdl"},
	{"name": "wood",     "sounds": ["debris/bustcrate1.wav", "debris/bustcrate2.wav"],
	 "gibs": "models/woodgibs.mdl"},
	{"name": "metal",    "sounds": ["debris/bustmetal1.wav", "debris/bustmetal2.wav"],
	 "gibs": "models/metalplategibs.mdl"},
	{"name": "flesh",    "sounds": ["debris/bustflesh1.wav", "debris/bustflesh2.wav"],
	 "gibs": "models/fleshgibs.mdl"},
	{"name": "cinder",   "sounds": ["debris/bustconcrete1.wav", "debris/bustconcrete2.wav"],
	 "gibs": "models/cindergibs.mdl"},
	{"name": "ceiling",  "sounds": ["debris/bustceiling.wav"],
	 "gibs": "models/ceilinggibs.mdl"},
	{"name": "computer", "sounds": ["buttons/spark5.wav", "buttons/spark6.wav"],
	 "gibs": ""},
	{"name": "unbreakable glass", "sounds": [], "gibs": ""},
	{"name": "rocks",    "sounds": ["debris/bustconcrete1.wav", "debris/bustconcrete2.wav"],
	 "gibs": "models/rockgibs.mdl"},
	{"name": "none",     "sounds": [], "gibs": ""},
]

## A rotating door's spawnflags: which axis it turns about, and which way.
##
## Z by default, which is Godot's Y. From CBaseToggle::AxisDir and the FGD.
const SF_ROT_REVERSE := 2
const SF_ROT_X_AXIS := 64
const SF_ROT_Y_AXIS := 128

## SF_DOOR_USE_ONLY. Without it a func_door opens when you walk into it, which is why nearly
## every door in Counter-Strike has no button.
const USE_ONLY := 256

var _lab: Node3D
var _doors: Array = []
var _buttons: Array = []

## Which breakable a collider belongs to, so a bullet that hits a shape knows what it hit.
var _breakables := {}

## Volumes the player's own movement has to know about: what you climb and what you swim in.
var ladders: Array[AABB] = []
var water: Array[AABB] = []


func setup(lab: Node3D, map: Node3D, entities: Array) -> void:
	_lab = lab
	var passable := 0

	for e in entities:
		var entity: Dictionary = e
		var model := str(entity.get("model", ""))
		if not model.begins_with("*"):
			continue
		var cls := str(entity.get("classname", ""))

		# Volumes first, because these need no geometry and usually have none. A func_ladder
		# is textured with something the renderer skips, so it produces no surfaces and no
		# node — and requiring a node found none of cs_siege's six. What a ladder is, is its
		# box.
		if cls == "func_ladder" and entity.has("bounds"):
			ladders.append(entity["bounds"] as AABB)

		var node := map.get_node_or_null("brush_%s" % model.substr(1)) as MeshInstance3D
		if node == null:
			continue
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
			"func_breakable":
				_build_breakable(node, entity)
			"func_water":
				# The surface is drawn separately and prettier; this is the brush behind it,
				# which you swim through rather than walk into.
				_make_passable(node)
				if entity.has("bounds"):
					water.append(entity["bounds"] as AABB)
			"func_ladder":
				# Its box was taken above; if it did happen to have geometry, it is not
				# something you bump into.
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
	var breakables := 0
	for key in _breakables:
		if (_breakables[key] as Dictionary)["node"] is MeshInstance3D:
			breakables += 1
	if breakables > 0:
		print("[brushes] %d breakable(s)" % int(breakables / 2.0))
	if not ladders.is_empty() or not water.is_empty():
		print("[brushes] %d ladder(s), %d water volume(s)" % [ladders.size(), water.size()])


## Something you can shoot to pieces.
##
## health is how much damage it takes, material is what it is made of, and both come from the
## map. de_aztec's crates declare 1 health, so a single shot does it, which is what makes them
## feel like scenery you can clear rather than an obstacle.
func _build_breakable(node: MeshInstance3D, entity: Dictionary) -> void:
	var kind := clampi(int(entity.get("material", 1)), 0, MATERIALS.size() - 1)
	var made_of: Dictionary = MATERIALS[kind]

	var sounds: Array[AudioStream] = []
	for named in made_of["sounds"]:
		var stream := _named_sound(str(named))
		if stream != null:
			sounds.append(stream)

	var record := {
		"node": node,
		"health": maxf(float(entity.get("health", 1.0)), 1.0),
		"material": str(made_of["name"]),
		"sounds": sounds,
		# Read now rather than when it breaks. A .mdl is fetched from the archive, decoded and
		# converted, and doing that at the moment somebody shoots stalls the frame hard enough
		# to hang the game for a second - which is exactly what it did.
		"gibs": _load_gibs(str(made_of["gibs"])),
		"broken": false,
	}
	_breakables[node.get_instance_id()] = record

	# Registered against every collider it has, because a shot arrives as a physics body and
	# has to be traced back to the thing it belongs to.
	for child in node.get_children():
		if child is StaticBody3D:
			_breakables[child.get_instance_id()] = record


## Something shot this. Returns true when it was something breakable.
func hurt(collider: Object, amount: float) -> bool:
	if collider == null:
		return false
	var record = _breakables.get(collider.get_instance_id())
	if record == null:
		return false

	var broken: Dictionary = record
	if broken["broken"]:
		return true
	broken["health"] = float(broken["health"]) - amount
	if float(broken["health"]) > 0.0:
		return true

	broken["broken"] = true
	_break(broken)
	return true


## Come apart: the noise, the pieces, and then gone.
func _break(record: Dictionary) -> void:
	var node: MeshInstance3D = record["node"]
	if not is_instance_valid(node):
		return

	var at := node.global_position
	var sounds: Array = record["sounds"]
	if not sounds.is_empty():
		var audio := AudioStreamPlayer3D.new()
		audio.stream = sounds[randi() % sounds.size()]
		audio.unit_size = 8.0
		_lab.add_child(audio)
		audio.global_position = at
		audio.play()
		audio.finished.connect(audio.queue_free)
		if _lab.has_method("track_sound"):
			_lab.track_sound(audio)

	_spawn_gibs(record, at, node)

	# Gone, rather than hidden: the game removes the entity, and leaving an invisible collider
	# behind is how a broken crate stays a wall.
	node.queue_free()
	print("[brushes] a %s breakable came apart" % str(record["material"]))


## The debris model, read once and kept.
##
## Shared between every breakable of the same material, because a level with eight crates
## should read models/rockgibs.mdl once rather than eight times.
static var _gib_cache := {}

func _load_gibs(named: String) -> Node3D:
	if named.is_empty() or _lab == null:
		return null
	if _gib_cache.has(named):
		return _gib_cache[named]

	var hit: Dictionary = _lab.vfs.find_files(named, PackedStringArray(["mdl"]),
		PackedStringArray(), PackedStringArray(), 1)
	var files: PackedStringArray = hit["files"]
	var built: Node3D = null
	if not files.is_empty():
		built = _lab.importer.load_model(str(files[0]))
		if built != null:
			# Kept out of the tree as a template to copy from.
			for c in built.get_children():
				if c is StaticBody3D:
					built.remove_child(c)
					c.queue_free()
	_gib_cache[named] = built
	return built


## The pieces it leaves, which are a real model the game ships.
##
## models/rockgibs.mdl and its kin are ordinary studio models with several body parts, one per
## fragment. Thrown as rigid bodies here, which GoldSrc does not do — it has its own gib
## physics — and is the modernisation rather than the restoration.
func _spawn_gibs(record: Dictionary, at: Vector3, was: MeshInstance3D) -> void:
	var piece = record["gibs"]
	if piece == null or not is_instance_valid(piece):
		return

	var spread: Vector3 = was.get_aabb().size * 0.4
	for i in 5:
		var body := RigidBody3D.new()
		var shape := CollisionShape3D.new()
		var box := BoxShape3D.new()
		box.size = Vector3(0.25, 0.25, 0.25)
		shape.shape = box
		body.add_child(shape)

		body.add_child((piece as Node3D).duplicate())

		_lab.add_child(body)
		body.global_position = at + Vector3(
			randf_range(-spread.x, spread.x), randf_range(0.0, spread.y),
			randf_range(-spread.z, spread.z))
		body.linear_velocity = Vector3(randf_range(-3, 3), randf_range(2, 5), randf_range(-3, 3))
		body.angular_velocity = Vector3(randf_range(-6, 6), randf_range(-6, 6), randf_range(-6, 6))
		# Cleared after a while: a level nobody restarts would otherwise fill with rubble.
		_lab.get_tree().create_timer(12.0).timeout.connect(body.queue_free)


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
	var pivot: Node3D = null
	var axis := Vector3.UP
	var swing := 0.0

	if rotating:
		# A brush mesh is built with its vertices already in world space and its node left at
		# the origin, so turning the node would swing the door around the middle of the map.
		# It needs something to turn about, so the node is hung under a pivot at its own
		# centre and shifted back by the same amount.
		#
		# GoldSrc turns it about the centre of an "origin brush" the mapper places inside it.
		# That brush is not distinguished here, so the centre of the whole thing is used
		# instead — right for a door that is roughly symmetrical and wrong for one hinged hard
		# to one side.
		var box: AABB = entity.get("bounds", AABB())
		var centre := box.position + box.size * 0.5

		pivot = Node3D.new()
		pivot.name = "%s_pivot" % node.name
		var parent := node.get_parent()
		var index := node.get_index()
		parent.remove_child(node)
		parent.add_child(pivot)
		parent.move_child(pivot, index)
		pivot.position = centre
		pivot.add_child(node)
		node.position = -centre

		var flags := int(entity.get("spawnflags", 0))
		# Valve's Z is Godot's Y, its X is Godot's -Z, its Y is Godot's -X.
		if (flags & SF_ROT_X_AXIS) != 0:
			axis = Vector3(0, 0, -1)
		elif (flags & SF_ROT_Y_AXIS) != 0:
			axis = Vector3(-1, 0, 0)
		swing = deg_to_rad(float(entity.get("distance", 90.0)))
		if (flags & SF_ROT_REVERSE) != 0:
			swing = -swing

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
		"pivot": pivot,
		"axis": axis,
		"shut": node.position,
		"open": node.position + travel,
		"rotating": rotating,
		"turn": swing,   # `distance` in degrees, 90 when the map does not say
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
		var where: Node3D = door["pivot"] if door["pivot"] != null else node
		var near := who.global_position.distance_to(where.global_position) < float(door["reach"])
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
				var turning: Node3D = door["pivot"]
				if turning != null and is_instance_valid(turning):
					turning.transform.basis = Basis(door["axis"] as Vector3,
						float(door["at"]) * float(door["turn"]))
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
