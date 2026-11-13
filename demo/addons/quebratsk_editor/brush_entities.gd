extends Node

## The parts of a level that are not scenery.
##
## Half of what a GoldSrc map contains is a brush entity: a door, a lift, a decoration you walk
## through, a wall you can break. They arrive as `brush_1`, `brush_2` … under the map, each
## with its own mesh and collider, and what each one should *do* is written in its classname.
##
## Everything here is the game's own behaviour, out of the Half-Life SDK's doors.cpp,
## buttons.cpp, triggers.cpp, plats.cpp and subs.cpp, and the FGD the level editors used.
## Nothing is invented; where a number is a choice it says so.
##
##   var brushes := BrushEntities.new()
##   add_child(brushes)
##   brushes.setup(host, map, importer.load_map_entities(uri))
##   brushes.actor = player
##
## `host` may be null. When it has a `vfs` the doors get their sounds, when it has an
## `importer` the breakables get their debris, and when it has `track_sound()` every sound
## made here is handed to it.

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

## A door's spawnflags, from doors.h.
##
## A rotating door turns about the level's Z by default, which is Godot's Y.
const SF_DOOR_START_OPEN := 1
const SF_ROT_REVERSE := 2
## Once triggered open it stays so until it is triggered again.
const SF_DOOR_NO_AUTO_RETURN := 32
const SF_ROT_X_AXIS := 64
const SF_ROT_Y_AXIS := 128
## Without it a func_door opens when you walk into it, which is why nearly every door in
## Counter-Strike has no button.
const USE_ONLY := 256

## A button's spawnflags, from buttons.cpp. Without the last one a button wants the use key.
const SF_BUTTON_DONTMOVE := 1
const SF_BUTTON_TOUCH_ONLY := 256

const SF_WALL_START_OFF := 1
const SF_RELAY_FIRE_ONCE := 1

## What a lift or a train sounds like, by the number the map gives in movesnd and stopsnd.
## The two lists are not the same length and do not pair up: number 3 moving is an elevator
## and number 3 stopping is a freight lift. From CBasePlatTrain::Precache in plats.cpp.
const PLAT_MOVE_SOUNDS := ["", "plats/bigmove1.wav", "plats/bigmove2.wav",
	"plats/elevmove1.wav", "plats/elevmove2.wav", "plats/elevmove3.wav",
	"plats/freightmove1.wav", "plats/freightmove2.wav", "plats/heavymove1.wav",
	"plats/rackmove1.wav", "plats/railmove1.wav", "plats/squeekmove1.wav",
	"plats/talkmove1.wav", "plats/talkmove2.wav"]
const PLAT_STOP_SOUNDS := ["", "plats/bigstop1.wav", "plats/bigstop2.wav",
	"plats/freightstop1.wav", "plats/heavystop2.wav", "plats/rackstop1.wav",
	"plats/railstop1.wav", "plats/squeekstop1.wav", "plats/talkstop1.wav"]

## func_rotating's flags, from util.h.
const SF_SPIN_INSTANT := 1
const SF_SPIN_BACKWARDS := 2
const SF_SPIN_Z_AXIS := 4
const SF_SPIN_X_AXIS := 8
const SF_SPIN_ACCDCC := 16
const SF_SPIN_NOT_SOLID := 64
const SF_SPIN_SMALL_RADIUS := 128
const SF_SPIN_MEDIUM_RADIUS := 256

## func_pendulum's, from util.h and doors.h. Its axis flags are a rotating door's.
const SF_PENDULUM_INSTANT := 1
const SF_PENDULUM_PASSABLE := 8
const SF_PENDULUM_AUTO_RETURN := 16

## func_train's and path_corner's, from plats.cpp and trains.h.
const SF_TRAIN_PASSABLE := 8
const SF_CORNER_WAIT_FOR_TRIGGER := 1
const SF_CORNER_TELEPORT := 2
const SF_CORNER_FIRE_ONCE := 4

## How long a lift stays at the top once nobody is on it. From plats.cpp.
const PLAT_HOLD := 3.0

## How near a thing has to be to count as touched, in metres. A choice: the game asks whether
## two boxes overlap, and here whoever walks about is a point.
const TOUCH := 0.75
## How close to a closing door counts as standing in its way, in metres from its box. About a
## player's half width.
const IN_THE_WAY := 0.4
## How loud a door, lift or button is, in dB, and the most it may reach however close you
## stand. At 0 dB a door slamming a metre away drowned the rifle.
const MACHINE_VOLUME := -9.0
const MACHINE_LOUDEST := -3.0

## Keys a multi_manager carries that are about itself. Every other key is something it fires
## and the value is how many seconds later.
const MANAGER_OWN_KEYS := ["classname", "origin", "position", "targetname", "spawnflags",
	"wait", "angles", "angle", "model", "bounds"]

## Whoever walks about in the level: what opens doors by touching them and sets off triggers.
## Left unset, the host's `_player` is used when it has one.
var actor: Node3D

## Whether things that want the use key also answer to being walked into. On, because a
## level is no use to somebody who has no use key yet; turn it off once `use()` is called
## from one.
var press_by_touch := true

var _host: Node
var _vfs: Object
var _importer: Object
var _now := 0.0

var _doors: Array = []
var _buttons: Array = []
var _plats: Array = []
var _triggers: Array = []
var _spinners: Array = []
var _pendulums: Array = []
var _trains: Array = []
## path_corner by targetname: where a train goes next and what it does when it gets there.
var _corners := {}
## targetname -> every record that answers to it.
var _named := {}
## Things fired with a delay: {"at": seconds, "target": name}.
var _pending: Array = []
## Targets something fired that nothing in the level answers to.
var _unanswered := {}

## Which breakable a collider belongs to, so a bullet that hits a shape knows what it hit.
var _breakables := {}

## Volumes the player's own movement has to know about: what you climb and what you swim in.
var ladders: Array[AABB] = []
var water: Array[AABB] = []


func setup(host: Node, map: Node3D, entities: Array) -> void:
	_host = host
	if host != null:
		_vfs = host.get("vfs")
		_importer = host.get("importer")
	var passable := 0

	for e in entities:
		var entity: Dictionary = e
		var cls := str(entity.get("classname", ""))

		# Things with no shape at all, which only pass a signal on.
		if cls == "multi_manager":
			var fires := {}
			for key in entity:
				if not MANAGER_OWN_KEYS.has(str(key)):
					# The editor writes a second use of the same name as `name#1`.
					fires[key] = float(str(entity[key]))
			_answer(entity, {"kind": "manager", "fires": fires})
			continue
		if cls == "trigger_relay":
			_answer(entity, {"kind": "relay", "target": str(entity.get("target", "")),
				"delay": float(entity.get("delay", 0.0)), "kill": str(entity.get("killtarget", "")),
				"once": (int(entity.get("spawnflags", 0)) & SF_RELAY_FIRE_ONCE) != 0, "spent": false})
			continue
		if cls == "path_corner":
			var called := str(entity.get("targetname", ""))
			if not called.is_empty() and entity.has("position"):
				_corners[called] = {"at": entity["position"] as Vector3,
					"target": str(entity.get("target", "")),
					"wait": float(entity.get("wait", 0.0)),
					"speed": float(entity.get("speed", 0.0)) * UNIT,
					"flags": int(entity.get("spawnflags", 0)),
					"message": str(entity.get("message", ""))}
			continue

		var model := str(entity.get("model", ""))
		if not model.begins_with("*"):
			continue

		# Volumes first, because these need no geometry and usually have none. A func_ladder
		# is textured with something the renderer skips, so it produces no surfaces and no
		# node — and requiring a node found none of cs_siege's six. What a ladder is, is its
		# box.
		if cls == "func_ladder" and entity.has("bounds"):
			ladders.append(entity["bounds"] as AABB)
		if (cls == "trigger_once" or cls == "trigger_multiple") and entity.has("bounds"):
			# trigger_once is a trigger_multiple that waits for ever. From triggers.cpp.
			var wait := -1.0 if cls == "trigger_once" else float(entity.get("wait", 0.2))
			_triggers.append({"kind": "trigger", "box": entity["bounds"] as AABB,
				"target": str(entity.get("target", "")), "kill": str(entity.get("killtarget", "")),
				"delay": float(entity.get("delay", 0.0)), "wait": wait, "ready": 0.0, "spent": false})

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
				_build_button(node, entity)
			"func_plat":
				_build_plat(node, entity)
			"func_rotating":
				_build_spinner(node, entity)
			"func_pendulum":
				_build_pendulum(node, entity)
			"func_train":
				_build_train(node, entity)
			"func_wall_toggle":
				var wall := {"kind": "wall", "node": node, "on": true}
				_answer(entity, wall)
				if (int(entity.get("spawnflags", 0)) & SF_WALL_START_OFF) != 0:
					_set_wall(wall, false)
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
			"trigger_once", "trigger_multiple":
				# A volume, never a wall and never seen.
				_make_passable(node)
				node.visible = false

	# A train goes to its first corner only once every corner has been read, because the map
	# lists them in no particular order. From CFuncTrain::Activate.
	for t in _trains:
		_train_start(t as Dictionary)

	if passable > 0:
		print("[brushes] %d brush(es) made passable" % passable)
	if not _spinners.is_empty() or not _pendulums.is_empty() or not _trains.is_empty():
		print("[brushes] %d spinning, %d swinging, %d train(s) on %d corner(s)"
			% [_spinners.size(), _pendulums.size(), _trains.size(), _corners.size()])
	if not _doors.is_empty():
		var voiced := 0
		for d in _doors:
			if (d as Dictionary)["move_sound"] != null: voiced += 1
		print("[brushes] %d door(s), %d with a sound" % [_doors.size(), voiced])
	if not _buttons.is_empty():
		print("[brushes] %d button(s)" % _buttons.size())
	if not _plats.is_empty():
		print("[brushes] %d lift(s)" % _plats.size())
	if not _triggers.is_empty():
		print("[brushes] %d trigger(s)" % _triggers.size())
	var breakables := summary()["breakables"] as int
	if breakables > 0:
		print("[brushes] %d breakable(s)" % breakables)
	if not ladders.is_empty() or not water.is_empty():
		print("[brushes] %d ladder(s), %d water volume(s)" % [ladders.size(), water.size()])


## Where the things that turn, swing and run on rails are, for whoever wants to go and look.
func moving_places() -> Array[Vector3]:
	var out: Array[Vector3] = []
	for record in _spinners + _pendulums:
		if is_instance_valid(record["pivot"]):
			out.append((record["pivot"] as Node3D).global_position)
	for train in _trains:
		if is_instance_valid(train["node"]):
			out.append((train["node"] as Node3D).global_position + (train["offset"] as Vector3))
	return out


## How many of each kind of thing the level turned out to have.
func summary() -> Dictionary:
	var broken := {}
	for key in _breakables:
		broken[(_breakables[key] as Dictionary)["node"]] = true
	return {"doors": _doors.size(), "buttons": _buttons.size(), "lifts": _plats.size(),
		"triggers": _triggers.size(), "breakables": broken.size(), "named": _named.size(),
		"ladders": ladders.size(), "water": water.size(), "spinners": _spinners.size(),
		"pendulums": _pendulums.size(), "trains": _trains.size(), "corners": _corners.size()}


## Names that were fired and that nothing in the level answered to: either a kind of entity
## not handled here yet, or the mapper's own loose end.
func unanswered() -> PackedStringArray:
	return PackedStringArray(_unanswered.keys())


## What a door, a lift or a wall is doing: "shut", "opening", "open", "closing", "on", "off".
## Empty when nothing answers to that name.
func state_of(targetname: String) -> String:
	for r in _named.get(targetname, []):
		var record: Dictionary = r
		if record.has("state"):
			return str(record["state"])
		if record["kind"] == "wall":
			return "on" if record["on"] else "off"
	return ""


## Set off everything called `target`, now or `delay` seconds from now. What a button, a
## trigger and a multi_manager all do; from SUB_UseTargets in subs.cpp.
func fire(target: String, delay: float = 0.0) -> void:
	if target.is_empty():
		return
	if delay > 0.0:
		_pending.append({"at": _now + delay, "target": target})
		return
	var answering: Array = _named.get(target, [])
	# Not everything with a name is a brush. A sound that waits to be triggered belongs to
	# whoever placed it, so the host is asked before the name is written down as unanswered.
	var elsewhere: bool = _host != null and _host.has_method("fire_target") 			and _host.fire_target(target)
	if answering.is_empty() and not elsewhere:
		_unanswered[target] = true
	for r in answering:
		_use(r as Dictionary)


## Take everything called `target` out of the level. What `killtarget` means.
func kill(target: String) -> void:
	for r in _named.get(target, []):
		var record: Dictionary = r
		record["dead"] = true
		# Something that turns hangs under a pivot that holds its sound too.
		var pivot = record.get("pivot")
		if pivot != null and is_instance_valid(pivot):
			(pivot as Node).queue_free()
			continue
		var node = record.get("node")
		if node != null and is_instance_valid(node):
			(node as Node).queue_free()
	_named.erase(target)


## The use key, pressed at `from` while looking along `toward`. Returns true when there was
## something there to use: a button, or a door that only opens this way.
func use(from: Vector3, toward: Vector3, reach: float = 1.7) -> bool:
	var best: Dictionary = {}
	var nearest := reach
	for list in [_buttons, _doors]:
		for r in list:
			var record: Dictionary = r
			if record.get("dead", false):
				continue
			if record["kind"] == "door" and not (record["use_only"] and str(record["name"]).is_empty()):
				continue
			var box := _box_now(record)
			var at := from.clamp(box.position, box.end)
			var gap := at.distance_to(from)
			if gap >= nearest:
				continue
			# Inside it, or facing it.
			if gap > 0.05 and (at - from).normalized().dot(toward.normalized()) < 0.3:
				continue
			nearest = gap
			best = record
	if best.is_empty():
		return false
	if best["kind"] == "button":
		_press(best)
	else:
		_use(best)
	return true


func _answer(entity: Dictionary, record: Dictionary) -> void:
	var called := str(entity.get("targetname", ""))
	record["name"] = called
	if called.is_empty():
		return
	if not _named.has(called):
		_named[called] = []
	(_named[called] as Array).append(record)


## Somebody's target was fired and this is one of the things that answer to it.
func _use(record: Dictionary) -> void:
	if record.get("dead", false):
		return
	match str(record["kind"]):
		"door":
			# From DoorActivate: a door that does not return by itself is shut by the next
			# signal; any other is only ever told to open.
			if record["state"] == "open" and record["toggles"]:
				_door_go(record, "closing")
			elif record["state"] == "shut" or record["state"] == "closing":
				_door_go(record, "opening")
		"plat":
			if record["state"] == "open":
				record["state"] = "closing"
			elif record["state"] == "shut":
				record["state"] = "opening"
		"wall":
			_set_wall(record, not record["on"])
		"manager":
			var fires: Dictionary = record["fires"]
			for key in fires:
				fire(str(key).get_slice("#", 0), float(fires[key]))
		"relay":
			if record["spent"]:
				return
			record["spent"] = record["once"]
			_send(record)
		"breakable":
			if not record["broken"]:
				record["broken"] = true
				_break(record)
		"spinner":
			_spin_toggle(record)
		"pendulum":
			_pendulum_use(record)
		"train":
			_train_use(record)


## Pass on what a button, a trigger or a relay was built to pass on.
func _send(record: Dictionary) -> void:
	if not str(record.get("kill", "")).is_empty():
		kill(str(record["kill"]))
	fire(str(record["target"]), float(record.get("delay", 0.0)))


func _set_wall(wall: Dictionary, on: bool) -> void:
	wall["on"] = on
	var node: MeshInstance3D = wall["node"]
	if not is_instance_valid(node):
		return
	node.visible = on
	for child in node.get_children():
		if child is CollisionObject3D:
			(child as CollisionObject3D).process_mode = \
				Node.PROCESS_MODE_INHERIT if on else Node.PROCESS_MODE_DISABLED


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
		"kind": "breakable",
		"node": node,
		"health": maxf(float(entity.get("health", 1.0)), 1.0),
		"material": str(made_of["name"]),
		"sounds": sounds,
		# Read now rather than when it breaks. A .mdl is fetched from the archive, decoded and
		# converted, and doing that at the moment somebody shoots stalls the frame hard enough
		# to hang the game for a second - which is exactly what it did.
		"gibs": _load_gibs(str(made_of["gibs"])),
		"broken": false,
		# What it sets off as it goes, which is how a vent cover opens the way behind it.
		"target": str(entity.get("target", "")),
	}
	_breakables[node.get_instance_id()] = record
	# A breakable with a name comes apart when that name is fired.
	_answer(entity, record)

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
	fire(str(record.get("target", "")))
	var node: MeshInstance3D = record["node"]
	if not is_instance_valid(node):
		return

	# The middle of what it is made of. A brush's vertices are already where they stand in
	# the level, so its node sits at the level's origin and says nothing about where it is.
	var at := node.global_transform * node.get_aabb().get_center()
	var sounds: Array = record["sounds"]
	if not sounds.is_empty():
		var audio := AudioStreamPlayer3D.new()
		audio.stream = sounds[randi() % sounds.size()]
		audio.unit_size = 8.0
		_stage().add_child(audio)
		audio.global_position = at
		audio.play()
		audio.finished.connect(audio.queue_free)
		_track(audio)

	_spawn_gibs(record, at, node)

	# Gone, rather than hidden: the game removes the entity, and leaving an invisible collider
	# behind is how a broken crate stays a wall.
	node.queue_free()
	print("[brushes] a %s breakable came apart" % str(record["material"]))


## Where sounds and debris are put: the host when there is one.
func _stage() -> Node:
	return _host if _host != null else self


func _track(audio: AudioStreamPlayer3D) -> void:
	if _host != null and _host.has_method("track_sound"):
		_host.track_sound(audio)


## The debris model, read once and kept.
##
## Shared between every breakable of the same material, because a level with eight crates
## should read models/rockgibs.mdl once rather than eight times.
static var _gib_cache := {}

func _load_gibs(named: String) -> Node3D:
	if named.is_empty() or _vfs == null or _importer == null:
		return null
	if _gib_cache.has(named) and is_instance_valid(_gib_cache[named]):
		return _gib_cache[named]

	var hit: Dictionary = _vfs.find_files(named, PackedStringArray(["mdl"]),
		PackedStringArray(), PackedStringArray(), 1)
	var files: PackedStringArray = hit["files"]
	var built: Node3D = null
	if not files.is_empty():
		built = _importer.load_model(str(files[0]))
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

		_stage().add_child(body)
		body.global_position = at + Vector3(
			randf_range(-spread.x, spread.x), randf_range(0.0, spread.y),
			randf_range(-spread.z, spread.z))
		body.linear_velocity = Vector3(randf_range(-3, 3), randf_range(2, 5), randf_range(-3, 3))
		body.angular_velocity = Vector3(randf_range(-6, 6), randf_range(-6, 6), randf_range(-6, 6))
		# Cleared after a while: a level nobody restarts would otherwise fill with rubble.
		get_tree().create_timer(12.0).timeout.connect(body.queue_free)


## Take the collider off, leaving what you can see.
func _make_passable(node: MeshInstance3D) -> void:
	for child in node.get_children():
		if child is StaticBody3D:
			node.remove_child(child)
			child.queue_free()


## Something pressed: by the use key, or by being walked into when it is built for that.
##
## `wait` is how long before it can be pressed again, and -1 means it stays pressed for good.
func _build_button(node: MeshInstance3D, entity: Dictionary) -> void:
	var flags := int(entity.get("spawnflags", 0))
	_buttons.append({
		"kind": "button",
		"node": node,
		"name": str(entity.get("targetname", "")),
		"box": entity.get("bounds", AABB()) as AABB,
		"target": str(entity.get("target", "")),
		"kill": str(entity.get("killtarget", "")),
		"sound": _named_sound(BUTTON_SOUNDS.get(int(entity.get("sounds", 0)), "")),
		"wait": float(entity.get("wait", 1.0)),
		"delay": float(entity.get("delay", 0.0)),
		"touch": (flags & SF_BUTTON_TOUCH_ONLY) != 0,
		"ready": 0.0,
		"spent": false,
		"audio": null,
	})


func _press(button: Dictionary) -> void:
	if button["spent"] or _now < float(button["ready"]):
		return
	var wait := float(button["wait"])
	if wait < 0.0:
		button["spent"] = true
	else:
		# Never quicker than half a second: a choice, so that standing against a button that
		# declares no wait does not fire it sixty times a second.
		button["ready"] = _now + maxf(wait, 0.5)
	var node = button["node"]
	if is_instance_valid(node):
		_play(button, button["sound"], node)
	_send(button)


## A lift you stand on.
##
## The mapper builds a func_plat where it stands when raised, and it starts lowered by
## `height`, or by its own height less 8 units when the map gives none. Stepping on takes it
## up; with nobody on it, it goes back down after a few seconds. One that has a name waits
## to be told instead, and starts raised. From plats.cpp.
func _build_plat(node: MeshInstance3D, entity: Dictionary) -> void:
	var box: AABB = entity.get("bounds", AABB())
	var drop := float(entity.get("height", 0.0)) * UNIT
	if drop <= 0.0:
		drop = maxf(box.size.y - 8.0 * UNIT, 0.05)
	var named := not str(entity.get("targetname", "")).is_empty()
	var plat := {
		"kind": "plat",
		"node": node,
		"box": box,
		"shut": node.position - Vector3(0.0, drop, 0.0),
		"open": node.position,
		"base": node.position,
		"speed": maxf(float(entity.get("speed", 150.0)) * UNIT, 0.2),
		"state": "open" if named else "shut",
		"at": 1.0 if named else 0.0,
		"hold": 0.0,
		"waits": named,
		"move_sound": _listed_sound(PLAT_MOVE_SOUNDS, int(entity.get("movesnd", 0))),
		"stop_sound": _listed_sound(PLAT_STOP_SOUNDS, int(entity.get("stopsnd", 0))),
		"audio": null,
	}
	if not named:
		node.position = plat["shut"]
	_answer(entity, plat)
	_plats.append(plat)


## Hang a brush under a pivot where it turns, and hand back the pivot.
##
## A brush with an origin brush is built around that point and its node already stands
## there; one without is built where it stands in the level, its node at the level's origin.
## Either way the pivot goes where the turning happens and the node moves under it by the
## opposite amount, so that nothing visibly shifts until the pivot turns.
##
## GoldSrc turns it about the centre of the origin brush, which reaches here as the entity's
## `origin`. One without turns about its own middle: right for something roughly symmetrical,
## wrong for a door hinged hard to one side, and there is nothing better to go by.
func _hang(node: MeshInstance3D, entity: Dictionary) -> Node3D:
	var box: AABB = entity.get("bounds", AABB())
	var centre := box.get_center()
	if entity.has("origin"):
		var raw := str(entity["origin"]).split_floats(" ", false)
		if raw.size() >= 3:
			# The level's (x, y, z) is Godot's (-y, z, -x).
			centre = Vector3(-raw[1], raw[2], -raw[0]) * UNIT
	var pivot := Node3D.new()
	pivot.name = "%s_pivot" % node.name
	var parent := node.get_parent()
	var index := node.get_index()
	parent.remove_child(node)
	parent.add_child(pivot)
	parent.move_child(pivot, index)
	pivot.position = centre
	pivot.add_child(node)
	# Relative to where it already stood. Writing -centre here put every door that has an
	# origin brush its own distance away from the doorway, since its node was not at zero.
	node.position = node.position - centre
	return pivot


## Valve's angles, (pitch, yaw, roll) in degrees, as a turn in Godot's axes.
##
## A brush is turned by its yaw about the level's up, then its pitch, then its roll, and
## Valve's up, its Y and its X are Godot's Y, -X and -Z.
static func _valve_basis(angles: Vector3) -> Basis:
	return Basis(Vector3.UP, deg_to_rad(angles.y)) \
		* Basis(Vector3(-1, 0, 0), deg_to_rad(angles.x)) \
		* Basis(Vector3(0, 0, -1), deg_to_rad(angles.z))


## Something that spins: a fan, a gear, a turning light.
##
## speed is degrees a second about one axis, up unless a flag says otherwise. One that
## speeds up and slows down (ACCDCC) gains speed × fanfriction/100 every tenth of a second
## until it is at full speed; one that does not starts at full speed but still runs down when
## it is stopped. Its sound is the wav in `message`, or one of five fan noises by `sounds`,
## and it rises in pitch and loudness with the speed. From CFuncRotating in bmodels.cpp.
func _build_spinner(node: MeshInstance3D, entity: Dictionary) -> void:
	var flags := int(entity.get("spawnflags", 0))
	var pivot := _hang(node, entity)
	if (flags & SF_SPIN_NOT_SOLID) != 0:
		# The fake beams of light that hang under a lamp: you walk through them.
		_make_passable(node)

	# Which of its angles it changes, in Valve's (pitch, yaw, roll) order.
	var turns := Vector3(0, 1, 0)
	if (flags & SF_SPIN_Z_AXIS) != 0:
		turns = Vector3(0, 0, 1)
	elif (flags & SF_SPIN_X_AXIS) != 0:
		turns = Vector3(1, 0, 0)
	if (flags & SF_SPIN_BACKWARDS) != 0:
		turns = -turns

	var friction := float(entity.get("fanfriction", 0.0)) / 100.0
	if friction == 0.0:
		friction = 1.0
	var volume := clampf(float(entity.get("volume", 0.0)) / 10.0, 0.0, 1.0)
	if volume == 0.0:
		volume = 1.0
	var sound: AudioStream = null
	var message := str(entity.get("message", ""))
	if not message.is_empty():
		sound = _named_sound(message, true)
	elif int(entity.get("sounds", 0)) in [1, 2, 3, 4, 5]:
		sound = _named_sound("fans/fan%d.wav" % int(entity.get("sounds", 0)), true)
	# How far it carries. The three sizes are the game's attenuations, and a sound in GoldSrc
	# fades out at about a thousand units divided by its attenuation.
	var attenuation := 0.8
	if (flags & SF_SPIN_SMALL_RADIUS) != 0:
		attenuation = 2.0
	elif (flags & SF_SPIN_MEDIUM_RADIUS) != 0:
		attenuation = 1.25

	var top := maxf(float(entity.get("speed", 0.0)), 0.0)
	var start := _angles_of(entity)
	pivot.transform.basis = _valve_basis(start)
	var spinner := {
		"kind": "spinner",
		"node": node,
		"pivot": pivot,
		"turns": turns,
		"start": start,
		"angle": 0.0,
		"top": top,
		"now": 0.0,
		# Degrees a second gained or lost each second: the tenth-of-a-second step, ten times.
		"rate": top * friction * 10.0,
		"gradual": (flags & SF_SPIN_ACCDCC) != 0,
		"want": false,
		"state": "stopped",
		"volume": volume,
		"sound": sound,
		"reach": 1000.0 / attenuation * UNIT,
		"audio": null,
		# A magic second and a half, the SDK's own, for the client to start up.
		"start_at": _now + 1.5 if (flags & SF_SPIN_INSTANT) != 0 else -1.0,
	}
	_answer(entity, spinner)
	_spinners.append(spinner)


## Switched on or off. One that is turning at all is told to stop, which is how a fan that
## is still running down can be switched off again but not straight back on.
func _spin_toggle(spinner: Dictionary) -> void:
	if float(spinner["now"]) != 0.0:
		spinner["want"] = false
		return
	spinner["want"] = true
	spinner["state"] = "spinning"
	if not spinner["gradual"]:
		spinner["now"] = spinner["top"]
	var audio := _spin_audio(spinner)
	if audio != null and not audio.playing:
		audio.play()


func _spin_audio(spinner: Dictionary) -> AudioStreamPlayer3D:
	if spinner["sound"] == null:
		return null
	if spinner["audio"] == null:
		var made := AudioStreamPlayer3D.new()
		made.stream = spinner["sound"]
		made.unit_size = 6.0
		made.max_distance = float(spinner["reach"])
		made.max_db = MACHINE_LOUDEST
		if AudioServer.get_bus_index(&"Rooms") >= 0:
			made.bus = &"Rooms"
		# On the pivot, not the brush, so that it stays where the thing turns.
		(spinner["pivot"] as Node3D).add_child(made)
		spinner["audio"] = made
	return spinner["audio"]


func _spin_step(spinner: Dictionary, delta: float) -> void:
	var pivot = spinner["pivot"]
	if spinner.get("dead", false) or not is_instance_valid(pivot):
		return
	var due := float(spinner["start_at"])
	if due >= 0.0 and _now >= due:
		spinner["start_at"] = -1.0
		_spin_toggle(spinner)

	var now := float(spinner["now"])
	var target: float = spinner["top"] if spinner["want"] else 0.0
	var rate := float(spinner["rate"]) * delta
	now = minf(now + rate, target) if now < target else maxf(now - rate, target)
	spinner["now"] = now

	var audio = spinner["audio"]
	if now <= 0.0 and not spinner["want"]:
		spinner["state"] = "stopped"
		if audio != null and (audio as AudioStreamPlayer3D).playing:
			(audio as AudioStreamPlayer3D).stop()
		return

	spinner["angle"] = fmod(float(spinner["angle"]) + now * delta, 360.0)
	(pivot as Node3D).transform.basis = _valve_basis(
		(spinner["start"] as Vector3) + (spinner["turns"] as Vector3) * float(spinner["angle"]))

	# Pitch from 30 to 100 percent and loudness from nothing to full with the speed, which is
	# the sound of a fan winding up. From RampPitchVol.
	if audio != null:
		var share := clampf(now / maxf(float(spinner["top"]), 0.001), 0.0, 1.0)
		var player := audio as AudioStreamPlayer3D
		player.pitch_scale = maxf((30.0 + 70.0 * share) / 100.0, 0.05)
		player.volume_db = MACHINE_VOLUME + linear_to_db(maxf(float(spinner["volume"]) * share, 0.001))


## Something that swings back and forth: a hanging chain, a lamp on a cable.
##
## It swings through `distance` degrees about one axis, from where it was built to the far
## side and back, at up to `speed` degrees a second. The acceleration is whatever gets it from
## rest to full speed over half the arc, which makes it slow at the ends and quick through the
## middle. `damp` makes each swing a little smaller until it hangs still. From CPendulum in
## bmodels.cpp.
func _build_pendulum(node: MeshInstance3D, entity: Dictionary) -> void:
	var flags := int(entity.get("spawnflags", 0))
	var pivot := _hang(node, entity)
	if (flags & SF_PENDULUM_PASSABLE) != 0:
		_make_passable(node)
	var start := _angles_of(entity)
	pivot.transform.basis = _valve_basis(start)
	var distance := float(entity.get("distance", 0.0))
	if distance == 0.0:
		# The SDK gives up on one with no arc: it hangs there and answers to nothing.
		return

	# The same axis flags as a rotating door. In Valve's (pitch, yaw, roll) order.
	var turns := Vector3(0, 1, 0)
	if (flags & SF_ROT_X_AXIS) != 0:
		turns = Vector3(0, 0, 1)
	elif (flags & SF_ROT_Y_AXIS) != 0:
		turns = Vector3(1, 0, 0)

	var top := float(entity.get("speed", 0.0))
	if top == 0.0:
		top = 100.0
	var pendulum := {
		"kind": "pendulum",
		"node": node,
		"pivot": pivot,
		"turns": turns,
		"start": start,
		"angle": 0.0,
		"middle": distance * 0.5,
		"top": top,
		"accel": top * top / (2.0 * absf(distance)),
		"damp": float(entity.get("damp", 0.0)) * 0.001,
		"damped": top,
		"speed": 0.0,
		"returns": (flags & SF_PENDULUM_AUTO_RETURN) != 0,
		"state": "still",
		"start_at": _now + 0.1 if (flags & SF_PENDULUM_INSTANT) != 0 else -1.0,
	}
	_answer(entity, pendulum)
	_pendulums.append(pendulum)


## Set swinging, or stopped: dead where it is, or brought back to where it was built when
## the mapper asked for that.
func _pendulum_use(pendulum: Dictionary) -> void:
	if pendulum["state"] == "still":
		pendulum["state"] = "swinging"
		pendulum["damped"] = pendulum["top"]
	elif pendulum["returns"]:
		pendulum["state"] = "returning"
	else:
		pendulum["state"] = "still"
		pendulum["speed"] = 0.0


func _pendulum_step(pendulum: Dictionary, delta: float) -> void:
	var pivot = pendulum["pivot"]
	if pendulum.get("dead", false) or not is_instance_valid(pivot):
		return
	var due := float(pendulum["start_at"])
	if due >= 0.0 and _now >= due:
		pendulum["start_at"] = -1.0
		_pendulum_use(pendulum)

	var angle := float(pendulum["angle"])
	match str(pendulum["state"]):
		"swinging":
			# Pulled back toward the middle of the arc from either side. From Swing.
			var top := float(pendulum["top"])
			var speed := float(pendulum["speed"])
			var pull := float(pendulum["accel"]) * delta
			speed += -pull if angle - float(pendulum["middle"]) > 0.0 else pull
			speed = clampf(speed, -top, top)
			var damp := float(pendulum["damp"])
			if damp > 0.0:
				var damped := float(pendulum["damped"]) - damp * float(pendulum["damped"]) * delta
				pendulum["damped"] = damped
				if damped < 30.0:
					# Too slow to be worth swinging: it comes to rest hanging in the middle.
					angle = float(pendulum["middle"])
					speed = 0.0
					pendulum["state"] = "still"
				else:
					speed = clampf(speed, -damped, damped)
			pendulum["speed"] = speed
			angle += speed * delta
		"returning":
			angle = move_toward(angle, 0.0, float(pendulum["top"]) * delta)
			if angle == 0.0:
				pendulum["state"] = "still"
				pendulum["speed"] = 0.0
		_:
			return
	pendulum["angle"] = angle
	(pivot as Node3D).transform.basis = _valve_basis(
		(pendulum["start"] as Vector3) + (pendulum["turns"] as Vector3) * angle)


## Something that runs from one path_corner to the next: a piston, a lift on a route, a
## train.
##
## It starts at the first corner its `target` names, placed so that the middle of its box is
## on the corner, and goes on to whatever each corner names next. A corner can change its
## speed, make it wait, set something off as it arrives, or send it straight to the next one.
## One with a name waits to be told; one without sets off at once. From CFuncTrain in
## plats.cpp.
func _build_train(node: MeshInstance3D, entity: Dictionary) -> void:
	var flags := int(entity.get("spawnflags", 0))
	if (flags & SF_TRAIN_PASSABLE) != 0:
		_make_passable(node)
	var box: AABB = entity.get("bounds", AABB())
	var speed := float(entity.get("speed", 0.0))
	if speed == 0.0:
		speed = 100.0
	var volume := float(entity.get("volume", 0.0)) * 0.1
	if volume == 0.0:
		volume = 0.85
	var train := {
		"kind": "train",
		"node": node,
		"box": box,
		"base": node.position,
		# From the node to the middle of its box, which is the point that rides the corners.
		"offset": box.get_center() - node.position,
		"speed": speed * UNIT,
		"first": str(entity.get("target", "")),
		"target": "",
		"current": "",
		"heading": "",
		"waits": not str(entity.get("targetname", "")).is_empty(),
		"held": false,
		"wait": 0.0,
		"goal": node.position,
		"until": 0.0,
		"state": "stopped",
		"volume": volume,
		"move_sound": _listed_sound(PLAT_MOVE_SOUNDS, int(entity.get("movesnd", 0)), true),
		"stop_sound": _listed_sound(PLAT_STOP_SOUNDS, int(entity.get("stopsnd", 0))),
		"audio": null,
		"stop_audio": null,
	}
	_answer(entity, train)
	_trains.append(train)


func _train_start(train: Dictionary) -> void:
	var first: Dictionary = _corners.get(train["first"], {})
	if first.is_empty():
		push_warning("[brushes] a func_train goes to '%s', which is no path_corner" % train["first"])
		return
	train["target"] = first["target"]
	train["current"] = train["first"]
	train["heading"] = train["first"]
	var node: Node3D = train["node"]
	node.position = (first["at"] as Vector3) - (train["offset"] as Vector3)
	if train["waits"]:
		train["held"] = true
	else:
		train["state"] = "waiting"
		train["until"] = _now + 0.1


## On to the next corner. From CFuncTrain::Next.
func _train_next(train: Dictionary) -> void:
	var called := str(train["target"])
	var next: Dictionary = _corners.get(called, {})
	if next.is_empty():
		# The end of a line that does not loop.
		_train_halt(train)
		return
	# The speed is the corner it is leaving's, when that corner says.
	var leaving: Dictionary = _corners.get(train["current"], {})
	if not leaving.is_empty() and float(leaving["speed"]) != 0.0:
		train["speed"] = leaving["speed"]
	train["target"] = next["target"]
	train["wait"] = next["wait"]
	train["current"] = called
	train["heading"] = called
	train["goal"] = (next["at"] as Vector3) - (train["offset"] as Vector3)
	if (int(next["flags"]) & SF_CORNER_TELEPORT) != 0:
		(train["node"] as Node3D).position = train["goal"]
		_train_arrive(train)
		return
	train["state"] = "moving"
	var audio := _train_audio(train)
	if audio != null and not audio.playing:
		audio.play()


## At a corner. From CFuncTrain::Wait.
func _train_arrive(train: Dictionary) -> void:
	var corner: Dictionary = _corners.get(train["current"], {})
	if not corner.is_empty() and not str(corner["message"]).is_empty():
		fire(str(corner["message"]))
		if (int(corner["flags"]) & SF_CORNER_FIRE_ONCE) != 0:
			corner["message"] = ""
	var told := not corner.is_empty() and (int(corner["flags"]) & SF_CORNER_WAIT_FOR_TRIGGER) != 0
	if told or train["held"]:
		train["held"] = true
		_train_halt(train)
		return
	var wait := float(train["wait"])
	train["state"] = "waiting"
	if wait != 0.0:
		_train_halt(train)
		train["state"] = "waiting"
		# -1 is for good.
		train["until"] = INF if wait < 0.0 else _now + wait
	else:
		# Straight on, on the next tick: a loop of corners that all send it onward at once
		# would otherwise never come back.
		train["until"] = _now


## Switched: a train that is going stops where it is, and one that is stopped goes on.
## From CFuncTrain::Use.
func _train_use(train: Dictionary) -> void:
	if train["held"]:
		train["held"] = false
		_train_next(train)
		return
	train["held"] = true
	# Back to the corner it was heading for, so that it carries on there when it is next told.
	if not str(train["heading"]).is_empty():
		train["target"] = train["heading"]
	_train_halt(train)


func _train_halt(train: Dictionary) -> void:
	train["state"] = "stopped"
	var audio = train["audio"]
	if audio != null and (audio as AudioStreamPlayer3D).playing:
		(audio as AudioStreamPlayer3D).stop()
	if train["stop_sound"] != null and is_instance_valid(train["node"]):
		var stop = train["stop_audio"]
		if stop == null:
			stop = _machine_player(train["node"] as Node3D, float(train["volume"]))
			train["stop_audio"] = stop
		(stop as AudioStreamPlayer3D).stream = train["stop_sound"]
		(stop as AudioStreamPlayer3D).play()


func _train_audio(train: Dictionary) -> AudioStreamPlayer3D:
	if train["move_sound"] == null or not is_instance_valid(train["node"]):
		return null
	if train["audio"] == null:
		var made := _machine_player(train["node"] as Node3D, float(train["volume"]))
		made.stream = train["move_sound"]
		train["audio"] = made
	return train["audio"]


## A sound that comes from the middle of a moving brush and goes with it.
func _machine_player(node: Node3D, volume: float) -> AudioStreamPlayer3D:
	var made := AudioStreamPlayer3D.new()
	made.unit_size = 6.0
	made.volume_db = MACHINE_VOLUME + linear_to_db(maxf(volume, 0.001))
	made.max_db = MACHINE_LOUDEST
	if AudioServer.get_bus_index(&"Rooms") >= 0:
		made.bus = &"Rooms"
	node.add_child(made)
	if node is VisualInstance3D:
		made.position = (node as VisualInstance3D).get_aabb().get_center()
	return made


func _train_step(train: Dictionary, delta: float) -> void:
	var node = train["node"]
	if train.get("dead", false) or not is_instance_valid(node):
		return
	match str(train["state"]):
		"moving":
			var goal: Vector3 = train["goal"]
			var here: Vector3 = (node as Node3D).position
			var stride := float(train["speed"]) * delta
			if here.distance_to(goal) <= stride:
				(node as Node3D).position = goal
				_train_arrive(train)
			else:
				(node as Node3D).position = here.move_toward(goal, stride)
		"waiting":
			if _now >= float(train["until"]):
				_train_next(train)


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
	var flags := int(entity.get("spawnflags", 0))
	var box: AABB = entity.get("bounds", AABB())

	if rotating:
		# Turned about a pivot, because a brush's node does not stand where it turns. See _hang.
		pivot = _hang(node, entity)

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

		var size: Vector3 = box.size
		var span := absf(size.x * dir.x) + absf(size.y * dir.y) + absf(size.z * dir.z)
		var lip := float(entity.get("lip", 0.0)) * UNIT
		travel = dir * maxf(span - lip, 0.05)

	# A door built open: the game stands it at the far end of its travel and swaps the two
	# ends, so that using it brings it back to where the mapper drew it. From CBaseDoor::Spawn.
	var starts_open := (flags & SF_DOOR_START_OPEN) != 0
	var door := {
		"kind": "door",
		"node": node,
		"pivot": pivot,
		"axis": axis,
		"box": box,
		"base": node.position,
		"shut": node.position + (travel if starts_open else Vector3.ZERO),
		"open": node.position + (Vector3.ZERO if starts_open else travel),
		"rotating": rotating,
		"turn_shut": swing if starts_open else 0.0,
		"turn_open": 0.0 if starts_open else swing,   # `distance` in degrees, 90 when the map does not say
		"speed": maxf(float(entity.get("speed", 100.0)) * UNIT, 0.2),
		"wait": float(entity.get("wait", 4.0)),
		"use_only": (flags & USE_ONLY) != 0,
		"toggles": (flags & SF_DOOR_NO_AUTO_RETURN) != 0,
		# What it sets off once it is fully open. From DoorHitTop.
		"target": str(entity.get("target", "")),
		"state": "shut",
		"at": 0.0,
		"hold": 0.0,
		# A rotating door declares neither movesnd nor stopsnd; what it has is locked_sound,
		# which is a button sound. Falling back to it is what gives those two a voice.
		"move_sound": _door_sound(MOVE_SOUNDS, int(entity.get("movesnd", 0)))
			if int(entity.get("movesnd", 0)) > 0
			else _named_sound(BUTTON_SOUNDS.get(int(entity.get("locked_sound", 0)), "")),
		"stop_sound": _door_sound(STOP_SOUNDS, int(entity.get("stopsnd", 0))),
		"audio": null,
	}
	_answer(entity, door)
	_doors.append(door)
	_place(door)


func _door_sound(pattern: String, index: int) -> AudioStream:
	if index <= 0:
		return null
	return _named_sound(pattern % index)


## One of a numbered list of sounds, where 0 and anything past the end are silence.
func _listed_sound(names: Array, index: int, looping := false) -> AudioStream:
	if index <= 0 or index >= names.size():
		return null
	return _named_sound(str(names[index]), looping)


func _named_sound(named: String, looping := false) -> AudioStream:
	if named.is_empty() or _vfs == null:
		return null
	var hit: Dictionary = _vfs.find_files("sound/" + named,
		PackedStringArray(["wav"]), PackedStringArray(), PackedStringArray(), 1)
	var files: PackedStringArray = hit["files"]
	if files.is_empty():
		return null
	return SoundLoader.load_sound(_vfs, str(files[0]), looping)


func _angles_of(entity: Dictionary) -> Vector3:
	var raw = entity.get("angles", "")
	if raw is Vector3:
		return raw as Vector3
	var parts := str(raw).split(" ", false)
	if parts.size() < 3:
		return Vector3.ZERO
	return Vector3(float(parts[0]), float(parts[1]), float(parts[2]))


## Where a moving thing's box is at this moment.
func _box_now(record: Dictionary) -> AABB:
	var box: AABB = record["box"]
	if record.has("base") and not record.get("rotating", false):
		var node = record["node"]
		if is_instance_valid(node):
			box.position += (node as Node3D).position - (record["base"] as Vector3)
	return box


## How far a point is from a box; nothing when it is inside.
func _gap(box: AABB, point: Vector3) -> float:
	return point.clamp(box.position, box.end).distance_to(point)


func _door_go(door: Dictionary, state: String) -> void:
	door["state"] = state
	var node = door["node"]
	if is_instance_valid(node):
		_play(door, door["move_sound"], node)


## Put a door or a lift where its `at` says it is: 0 shut, 1 open.
func _place(record: Dictionary) -> void:
	var node = record["node"]
	if not is_instance_valid(node):
		return
	var at := float(record["at"])
	if record.get("rotating", false):
		var turning = record["pivot"]
		if turning != null and is_instance_valid(turning):
			(turning as Node3D).transform.basis = Basis(record["axis"] as Vector3,
				lerpf(float(record["turn_shut"]), float(record["turn_open"]), at))
	else:
		(node as Node3D).position = (record["shut"] as Vector3).lerp(record["open"] as Vector3, at)


## Move a door or a lift along by one step. Returns true on the step it arrives.
func _travel(record: Dictionary, delta: float) -> bool:
	var opening: bool = record["state"] == "opening"
	if not opening and record["state"] != "closing":
		return false
	var step: float = float(record["speed"]) * delta
	var span: float = (record["open"] as Vector3).distance_to(record["shut"] as Vector3)
	# A turning door's speed is degrees a second.
	var per: float = step / maxf(span, 0.01)
	if record.get("rotating", false):
		per = deg_to_rad(float(record["speed"]) / UNIT) * delta \
			/ maxf(absf(float(record["turn_open"]) - float(record["turn_shut"])), 0.01)
	record["at"] = clampf(float(record["at"]) + (per if opening else -per), 0.0, 1.0)
	_place(record)
	if opening and float(record["at"]) >= 1.0:
		record["state"] = "open"
		return true
	if not opening and float(record["at"]) <= 0.0:
		record["state"] = "shut"
		return true
	return false


func _physics_process(delta: float) -> void:
	step(delta)


## One tick of everything that moves or waits. Called every physics frame; a test may call
## it by hand instead.
func step(delta: float) -> void:
	_now += delta

	var due: Array = []
	for p in _pending:
		if float((p as Dictionary)["at"]) <= _now:
			due.append(p)
	for p in due:
		_pending.erase(p)
		fire(str((p as Dictionary)["target"]))

	var who: Node3D = actor
	if (who == null or not is_instance_valid(who)) and _host != null:
		who = _host.get("_player") as Node3D
	var here := Vector3.ZERO
	var present := who != null and is_instance_valid(who)
	if present:
		here = who.global_position

	for t in _triggers:
		var trigger: Dictionary = t
		if not present or trigger["spent"] or _now < float(trigger["ready"]):
			continue
		if not (trigger["box"] as AABB).grow(0.3).has_point(here):
			continue
		if float(trigger["wait"]) < 0.0:
			trigger["spent"] = true
		else:
			trigger["ready"] = _now + float(trigger["wait"])
		_send(trigger)

	for bt in _buttons:
		var button: Dictionary = bt
		if not present or button.get("dead", false):
			continue
		if not (button["touch"] or press_by_touch):
			continue
		if _gap(button["box"] as AABB, here) < TOUCH:
			_press(button)

	for d in _doors:
		var door: Dictionary = d
		if door.get("dead", false) or not is_instance_valid(door["node"]):
			continue

		# Touch opens it, which is what a func_door without SF_USE_ONLY does — unless it has
		# a name: a door that is somebody's target only ever answers to that. From DoorTouch.
		var near := present and _gap(_box_now(door), here) < TOUCH
		var by_touch: bool = str(door["name"]).is_empty() and (not door["use_only"] or press_by_touch)
		if door["state"] == "shut" and near and by_touch:
			_door_go(door, "opening")
		elif door["state"] == "closing" and present and float(door["wait"]) >= 0.0 and _gap(_box_now(door), here) < IN_THE_WAY:
			# Someone in the doorway while it closes: it goes back up, which is what Blocked
			# does for a door with a wait. Letting it finish and then reopening on touch is
			# the arrival bang and the whole move again, from right beside your head.
			_door_go(door, "opening")
		elif door["state"] == "open" and not door["toggles"]:
			door["hold"] = float(door["hold"]) - delta
			# wait -1 means it stays open, which is how a lift's landing door behaves.
			if float(door["wait"]) >= 0.0 and float(door["hold"]) <= 0.0 and not near:
				_door_go(door, "closing")

		if _travel(door, delta):
			_play(door, door["stop_sound"], door["node"])
			if door["state"] == "open":
				door["hold"] = float(door["wait"])
				fire(str(door["target"]))

	for p in _plats:
		var plat: Dictionary = p
		if plat.get("dead", false) or not is_instance_valid(plat["node"]):
			continue
		var box := _box_now(plat)
		# On it, or in the shaft above it: within its outline and no lower than its top.
		var aboard := present and here.x > box.position.x and here.x < box.end.x \
			and here.z > box.position.z and here.z < box.end.z \
			and here.y > box.end.y - 0.3 and here.y < box.end.y + 2.5
		if not plat["waits"]:
			if plat["state"] == "shut" and aboard:
				plat["state"] = "opening"
				_play(plat, plat["move_sound"], plat["node"])
			elif plat["state"] == "open":
				plat["hold"] = PLAT_HOLD if aboard else float(plat["hold"]) - delta
				if float(plat["hold"]) <= 0.0:
					plat["state"] = "closing"
					_play(plat, plat["move_sound"], plat["node"])
		if _travel(plat, delta):
			_play(plat, plat["stop_sound"], plat["node"])
			plat["hold"] = PLAT_HOLD

	for s in _spinners:
		_spin_step(s as Dictionary, delta)
	for p in _pendulums:
		_pendulum_step(p as Dictionary, delta)
	for t in _trains:
		_train_step(t as Dictionary, delta)


func _play(record: Dictionary, stream: AudioStream, node: Node3D) -> void:
	if stream == null:
		return
	if record["audio"] == null:
		var made := AudioStreamPlayer3D.new()
		made.unit_size = 6.0
		made.volume_db = MACHINE_VOLUME
		made.max_db = MACHINE_LOUDEST
		node.add_child(made)
		record["audio"] = made
		# Through the room's echo but not followed for walls: the sound comes from the middle
		# of the door, which is inside the door, so it counted as behind a wall until the
		# door had moved out of the way and then jumped to full loudness as you went through.
		if AudioServer.get_bus_index(&"Rooms") >= 0:
			made.bus = &"Rooms"
	var audio: AudioStreamPlayer3D = record["audio"]
	# From the middle of the thing, wherever it has got to. A brush's node sits at the level's
	# origin, and a sound left there is a button heard from the far side of the map.
	if node is VisualInstance3D:
		audio.position = (node as VisualInstance3D).get_aabb().get_center()
	audio.stream = stream
	audio.play()
