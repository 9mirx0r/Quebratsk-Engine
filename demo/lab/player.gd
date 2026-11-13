extends CharacterBody3D

## What a ray may hit: everything but the level fattened by half a player (layer 20), which
## exists only for the point at a player's middle and stands 0.9 m proud of every floor.
const SEEN_BY_RAYS := 0xFFFFFFFF & ~(1 << 19)

## First-person controller, on the engine's own numbers.
##
## Every constant here is a GoldSrc cvar times 0.0254, because a Hammer unit is an inch. None
## of them is a value that felt about right — an earlier version of this had 5.0, 22.0 and 6.5
## in it and none of those came from anywhere.
##
## What the model does is not decided here. The preset says which of its hundred and eleven
## sequences is walking and which is crouched, and which of a weapon's six is firing.

const AnimationSets := preload("res://addons/quebratsk_editor/animation_sets.gd")
const SoundCatalogue := preload("res://addons/quebratsk_editor/sound_catalogue.gd")
const SoundLoader := preload("res://addons/quebratsk_editor/sound_loader.gd")
const ModelPreset := preload("res://addons/quebratsk_editor/model_preset.gd")
const Stairs := preload("res://lab/stairs.gd")
const WeaponManifest := preload("res://addons/quebratsk_editor/weapon_manifest.gd")
const Figure := preload("res://lab/figure.gd")

const UNIT := 0.0254

const SPEED := 250.0 * UNIT          # unencumbered; the weapon lowers it
const GRAVITY := 800.0 * UNIT        # sv_gravity 800
const JUMP := 268.328 * UNIT         # sqrt(2 * 800 * 45): clears a 45 unit crate
const WALK_FACTOR := 0.52            # cl_movespeedkey
const CROUCH_FACTOR := 0.333         # PLAYER_DUCKING_MULTIPLIER

## The two hulls and the eye height in each. 72 units standing with the view at 64
## (VEC_VIEW), 36 ducked with it at 28 (VEC_DUCK_VIEW).
##
## Both eye heights are measured from the SOLES, and that is the whole of the trap. A GoldSrc
## body's origin is the centre of its hull, most of a metre above the feet, so putting the
## camera 64 units above the origin puts it 64 above the middle of the chest: two and a half
## metres off the ground instead of one and a half.
##
## Nothing about the body moves when that is wrong. You stand correctly, your shadow is
## correct, every measurement of the collider is correct, and the world is a metre further
## down than it should be — which reads as floating, and is the same confusion that made the
## collider two and a half metres tall, showing up in a second place.
const STAND_HULL := 72.0 * UNIT
const DUCK_HULL := 36.0 * UNIT
const EYE := 64.0 * UNIT
const DUCK_EYE := 28.0 * UNIT

const ACCELERATE := 5.0
const AIR_ACCELERATE := 10.0
const FRICTION := 4.0
const STOP_SPEED := 75.0 * UNIT

## The clamp that makes air control a skill rather than free speed. Acceleration in the air is
## computed against 30 units per second however fast the player wants to go, so turning into
## the movement grants a sliver at a time. Bunny hopping comes out of this one line.
const MAX_AIR_SPEED := 30.0 * UNIT

## V_CalcBob and V_CalcRoll: the camera's rise and lean while walking is code, not an
## animation of the head. GoldSrc has no bone driving it.
const BOB := 0.01
const BOB_CYCLE := 0.8
const BOB_UP := 0.5
const ROLL_ANGLE := 2.0
const ROLL_SPEED := 200.0 * UNIT

## Time between footsteps, and the speeds that decide which. From PM_UpdateStepSound.
##
## Time, not distance. An earlier version of this counted metres covered and said in a comment
## that GoldSrc does the same, which is simply false: flTimeStepSound is 400 milliseconds at a
## walk and 300 at a run, and the choice between them is made at velrun. Reading the code was
## what settled it.
const STEP_INTERVAL_WALK := 0.40
const STEP_INTERVAL_RUN := 0.30
const VEL_WALK := 120.0 * UNIT
const VEL_RUN := 210.0 * UNIT
# 0.2 and 0.5 in the engine's linear scale, counted down from the rifle rather than from full:
# a footstep at half of everything was louder than the gun, which plays at -14.
const STEP_VOLUME_WALK := -28.0
const STEP_VOLUME_RUN := -20.0

## MAX_CLIMB_SPEED, and the shove a jump gives you off a ladder.
const CLIMB_SPEED := 200.0 * UNIT
const LADDER_JUMP := 270.0 * UNIT
## How near a ladder's volume the hull has to come to be on it, in metres, and how often a
## rung is heard under a hand: the engine's 350 milliseconds.
const LADDER_REACH := 0.12
const LADDER_STEP := 0.35

## In water everything is slower and the drag is its own. wishspeed *= 0.8 in PM_WaterMove.
const WATER_SPEED := 0.8

## Doing nothing in water sinks you, at a fixed rate rather than at an accelerating one:
## PM_WaterMove sets a wished-for downward velocity of 60 units per second and leaves it there.
## An earlier version applied a fraction of gravity, which sinks faster and faster and is not
## what floating in water feels like.
const WATER_SINK := 60.0 * UNIT

## How long the feet may leave the ground before it counts as being in the air.
const COYOTE := 0.12

const LOOK := 0.0022
const RANGE := 80.0
const DAMAGE := 34

var health := 100

var _lab: Node3D
var _camera: Camera3D
var _pitch := 0.0
var _speed := SPEED
var _eye_rest := Vector3(0, EYE, 0)

## How far below the body's origin the feet are, taken from the collider rather than assumed.
## Every eye height is measured up from there.
var _soles := 0.0
var _bob_time := 0.0

## What the weapon hangs from, so it can be moved without touching the model's own mirror.
var _view_pivot: Node3D
## How far the hands still are behind where the eyes have turned to, in radians.
var _sway := Vector2.ZERO
## How far the view is still pressed down by the last landing, in metres.
var _dip := 0.0
var _fall_speed := 0.0
var _was_falling := false
var _flash: OmniLight3D

## The player's own figure, and whether it is being drawn. The game never draws it from its
## own eyes; with the look improved it is, so looking down there are legs and feet there.
var _body: Skeleton3D
var _body_shown := false
## The copies of the figure's meshes that are drawn. The figure itself goes on casting the
## whole shadow; these are cut off below the chest and cast none.
var _legs: Array[MeshInstance3D] = []
## The first and last of the bones that carry what is not drawn: in these models everything
## from the chest up, arms included, is numbered in one run between the pelvis and the legs.
const HIDE_FROM := "Bip01 Spine1"
const HIDE_TO := "-- Neck smooth"
var _airborne := 0.0
## What the figure's arms do over its legs: holding the weapon up, and firing it. Nobody sees
## them but the shadow, which without them walks about with its arms straight out.
var _aim_move := ""
var _shoot_move := ""
var _shoot_until := 0.0
var _legs_now := ""
## The flash at the end of the barrel, as the eye sees it.
var _muzzle: Sprite3D

## Whether damage is being ignored, so the HUD can say so.
func is_immortal() -> bool:
	return _god


## Walk through walls, on V.
##
## Here because the doors do not open yet: a func_door's geometry is baked into the world mesh
## along with everything else that shares its texture, so it is a wall until brush models are
## built as their own meshes. Until then a level with a shut door is a level you cannot leave,
## and being stuck in a room is a poor way to look at the rest of it.
var _noclip := false

## Damage is reported and never applied. On by default, because this is a bench for looking
## at a level rather than a fight, and three enemies opening up on you while you are trying to
## see whether a door works is not a test of anything. G turns it off.
var _god := true

var _stances := {}
var _body_anim: AnimationPlayer
var _hull: CollisionShape3D
## How far the eye still is below where a step just put it. The body goes up a stair at once
## and the view follows over a moment, or every stair is a jolt.
var _stair_lag := 0.0
var _stepped_until := 0.0
var _stand_height := 0.0
var _stand_y := 0.0
var _ducked := false

var _view_anim: AnimationPlayer
var _attacks := PackedStringArray()
var _reload_chain := PackedStringArray()
## The sequence the weapon rests in, or "" when its model names none.
var _view_idle := ""

## Rounds in the weapon, how many it holds, and how many are carried besides. The size is the
## game's own figure for the weapon; a knife has none, and then nothing here counts.
var _clip := 0
var _clip_size := 0
var _reserve := 0
var _refill_at := 0.0
var _dry_click: AudioStream

## The noises a reload makes, in the order the weapon makes them, and a player to make them
## with. Separate from the gunshot so a magazine going in is not cut off by a shot.
var _reload_sounds: Array[AudioStream] = []
var _reload_times := PackedFloat32Array()
var _reload_audio: AudioStreamPlayer3D
var _reload_queue := 0
var _reload_started := 0.0
var _gunshot: AudioStream
var _audio: AudioStreamPlayer3D
var _cycle := 0.0
var _next_shot := 0.0
var _busy_until := 0.0

## Where you can climb and where you can swim, from the map's own brush volumes.
var _ladders: Array[AABB] = []
var _water: Array[AABB] = []
var _on_ladder := false
## Which way the ladder being climbed faces: out of the wall it is on, level.
var _ladder_out := Vector3.ZERO
var _submerged := 0.0

## Footstep samples per surface, loaded the first time that surface is walked on. A level
## uses three or four kinds of floor and there is no reason to read all nine.
var _steps_by_surface := {}
## Where each mesh's surfaces begin, in triangles, so a ray's face index says which one it hit.
var _face_table := {}
var _surface_now := "step"

var _step_audio: AudioStreamPlayer3D
var _next_step := 0.0
var _last_step := -1


func _ready() -> void:
	_audio = AudioStreamPlayer3D.new()
	# The weapon is at the camera, so 3D attenuation puts it on top of the listener. This is a
	# gunshot fired next to someone's ear.
	_audio.volume_db = -14.0
	_audio.unit_size = 3.0
	add_child(_audio)

	_step_audio = AudioStreamPlayer3D.new()
	_step_audio.volume_db = -8.0
	_step_audio.unit_size = 4.0
	add_child(_step_audio)

	_reload_audio = AudioStreamPlayer3D.new()
	_reload_audio.volume_db = -6.0
	_reload_audio.unit_size = 3.0
	add_child(_reload_audio)

	for child in get_children():
		if child is CollisionShape3D:
			_hull = child as CollisionShape3D
			break
	if _hull != null and _hull.shape is CapsuleShape3D:
		# Duplicated: the importer hands the same shape to every character it builds, and
		# resizing a shared one would duck every enemy in the level at once.
		_hull.shape = _hull.shape.duplicate()
		_stand_height = (_hull.shape as CapsuleShape3D).height
		_stand_y = _hull.position.y
		# The bottom of the collider is where this body's feet are, and every eye height in
		# the engine is measured up from there.
		_soles = _stand_y - _stand_height * 0.5
	_eye_rest = Vector3(0, _soles + EYE, 0)

	Input.mouse_mode = Input.MOUSE_MODE_CAPTURED


## The volumes the level says you can climb and swim in.
func set_volumes(ladders: Array, water: Array) -> void:
	_ladders.clear()
	_water.clear()
	for box in ladders:
		_ladders.append(box as AABB)
	for box in water:
		_water.append(box as AABB)
	if not _ladders.is_empty() or not _water.is_empty():
		print("[player] %d ladder(s), %d water volume(s)" % [_ladders.size(), _water.size()])


func setup(lab: Node3D, camera: Camera3D, preset: Dictionary, weapon: Dictionary) -> void:
	_lab = lab
	_camera = camera
	_stances = preset.get("stances", {})

	for child in get_children():
		if child is Skeleton3D:
			_body = child as Skeleton3D
			_body_anim = child.get_node_or_null("AnimationPlayer")
	if _body != null:
		for part in _body.get_children():
			if part is MeshInstance3D:
				var twin := part.duplicate() as MeshInstance3D
				twin.name = "Legs%d" % _legs.size()
				twin.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
				twin.visible = false
				# Stood a little behind the eye. Straight under it the eye looks down the
				# inside of the legs, which are hollow, and sees the floor through them.
				twin.position.z = 0.3
				_legs.append(twin)
		for twin in _legs:
			_body.add_child(twin)
		_take_up(weapon)
		if _lab.get("floor_light") != null:
			_lab.floor_light.dress(_body, -_soles)
		var first := _body.find_bone(HIDE_FROM)
		var last := _body.find_bone(HIDE_TO)
		if first < 0 or last < first:
			# A figure built some other way: better no legs than a head in the way.
			for twin in _legs:
				twin.queue_free()
			_legs.clear()
		for twin in _legs:
			twin.set_instance_shader_parameter("hide_bones", Vector2(first, last))

	SoundCatalogue.read_materials(_lab.vfs)

	camera.position = _eye_rest
	print("[player] eye %.2f m above the soles (VEC_VIEW is %.2f)"
		% [EYE, 64.0 * UNIT])

	# Through the room, so a gunshot in a tunnel is a gunshot in a tunnel. Not tracked for
	# occlusion: these happen at the listener's own head, and nothing can be between you and
	# your own rifle.
	for mouth in [_audio, _step_audio, _reload_audio]:
		if mouth != null:
			mouth.bus = "Rooms"

	if not weapon.is_empty():
		_arm(weapon)


## The weapon as the shadow has it: in the figure's hand, with the arms that hold it.
func _take_up(weapon: Dictionary) -> void:
	var held := ""
	if not weapon.is_empty():
		held = str(weapon.get("holder_class", ""))
		if held.is_empty():
			held = "mp5"
	if _body_anim != null and not held.is_empty():
		if _body_anim.has_animation("ref_aim_" + held):
			_aim_move = "ref_aim_" + held
		if _body_anim.has_animation("ref_shoot_" + held):
			_shoot_move = "ref_shoot_" + held
	var model := Figure.hold(_lab, _body, weapon, "the player")
	if model != null:
		# Never drawn, like the figure that holds it: only its shadow.
		var parts: Array = model.find_children("*", "MeshInstance3D", true, false)
		if model is MeshInstance3D:
			parts.append(model)
		for part in parts:
			(part as MeshInstance3D).cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_SHADOWS_ONLY
	print("[player] the figure aims with '%s', fires with '%s', %s in its hand"
		% [_aim_move, _shoot_move, "a weapon" if model != null else "nothing"])


## Hold the weapon: its own model in front of the camera, and what the game says it does.
func _arm(weapon: Dictionary) -> void:
	_cycle = float(weapon.get("cycle_time", 0.0))
	_clip_size = int(WeaponManifest.lookup(str(weapon.get("model", weapon.get("name", "")))).get("clip_size", 0))
	if _clip_size <= 0:
		_clip_size = int(WeaponManifest.lookup(str(weapon.get("name", ""))).get("clip_size", 0))
	_clip = _clip_size
	_reserve = _clip_size * 3
	var dry := ModelPreset.resolve(_lab.vfs, "sound/weapons/dryfire_rifle.wav")
	_dry_click = SoundLoader.load_sound(_lab.vfs, dry) if not dry.is_empty() else null
	var declared := float(weapon.get("max_speed", 0.0))
	_speed = declared if declared > 0.0 else SPEED

	# Its rest and its coming into the hands are read beside what the preset names.
	var rest := {}
	var weapon_uri := ModelPreset.resolve(_lab.vfs, str(weapon.get("model", "")))
	if not weapon_uri.is_empty():
		rest = AnimationSets.view_rest(_lab.importer.list_poses(weapon_uri))
	var also := PackedStringArray()
	for role in rest:
		also.append(str(rest[role]))
	_view_idle = str(rest.get("idle", ""))

	var model := ModelPreset.spawn(_lab.importer, _lab.vfs, weapon, also)
	if model == null:
		push_warning("[player] the weapon preset could not be built")
		return

	# A view model has no business colliding with anything.
	for child in model.get_children():
		if child is StaticBody3D:
			model.remove_child(child)
			child.queue_free()
	model.position = Vector3(0.0, -0.06, -0.08)
	_view_pivot = Node3D.new()
	_view_pivot.name = "ViewPivot"
	_camera.add_child(_view_pivot)
	_view_pivot.add_child(model)

	# In the right hand. Counter-Strike's view models are built held in the left and the game
	# draws them mirrored unless told otherwise (cl_righthand, on by default), so the same
	# mirror is applied here. Godot notices a mirrored node and turns its triangles round by
	# itself; the materials are left exactly as they are.
	model.scale = Vector3(-1.0, 1.0, 1.0)

	# Lit by the floor the player is standing on, as the game lights a view model: the hands
	# go dark in a dark room along with everything else.
	if _lab.get("floor_light") != null:
		_lab.floor_light.dress(model)

	# On a layer of its own, so the light of its own shot can be kept off it: see flash.gd.
	# And throwing no shadow. It hangs at the eye, so its shadow is a rifle and a pair of arms
	# floating at the height of the head of the figure's own shadow.
	for part in model.find_children("*", "MeshInstance3D", true, false):
		(part as MeshInstance3D).layers = 2
		(part as MeshInstance3D).cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	_place_muzzle(model, weapon_uri)

	_view_anim = model.get_node_or_null("AnimationPlayer")
	if _view_anim == null:
		for child in model.get_children():
			if child is Skeleton3D:
				_view_anim = child.get_node_or_null("AnimationPlayer")
	if _view_anim != null:
		var has := PackedStringArray(_view_anim.get_animation_list())
		_attacks = AnimationSets.attacks(has)
		_reload_chain = AnimationSets.reload_chain(has)
		# Brought up into the hands, then at rest, which is where it goes back to after
		# anything else it does.
		if not _view_anim.has_animation(_view_idle):
			_view_idle = ""
		var draw := str(rest.get("draw", ""))
		if _view_anim.has_animation(draw):
			_view_anim.play(draw)
			_busy_until = Time.get_ticks_msec() / 1000.0 + _view_anim.get_animation(draw).length * 0.6
			_rest_after()
		elif not _view_idle.is_empty():
			_view_anim.play(_view_idle)

	for path in weapon.get("fire_sounds", []):
		var uri := ModelPreset.resolve(_lab.vfs, str(path))
		if uri.is_empty():
			continue
		_gunshot = SoundLoader.load_sound(_lab.vfs, uri)
		if _gunshot != null:
			break

	_load_reload_sounds(weapon)
	print("[player] %s: %d attack(s), reload in %d step(s), gunshot %s"
		% [weapon.get("name", "?"), _attacks.size(), _reload_chain.size(),
		   "yes" if _gunshot != null else "MISSING"])


## The flash, on the point the weapon's own model marks as the end of its barrel.
func _place_muzzle(model: Node3D, weapon_uri: String) -> void:
	if weapon_uri.is_empty():
		return
	var skeleton: Skeleton3D = model as Skeleton3D
	if skeleton == null:
		for child in model.get_children():
			if child is Skeleton3D:
				skeleton = child as Skeleton3D
	var points: Array = _lab.importer.list_attachments(weapon_uri)
	if skeleton == null or points.is_empty():
		print("[player] this weapon marks no muzzle, so it has no flash")
		return
	# The first is the muzzle; the others are where the shells come out.
	var point: Dictionary = points[0]
	var bone := int(point.get("bone", -1))
	if bone < 0 or bone >= skeleton.get_bone_count():
		return
	_muzzle = Figure.muzzle_flash(_lab, Figure.flash_kind(_lab, weapon_uri))
	if _muzzle == null:
		return
	var socket := BoneAttachment3D.new()
	socket.name = "Muzzle"
	socket.bone_idx = bone
	skeleton.add_child(socket)
	socket.add_child(_muzzle)
	_muzzle.position = point.get("position", Vector3.ZERO) as Vector3
	_muzzle.layers = 2
	print("[player] muzzle on bone '%s' at %s, flash %s" % [skeleton.get_bone_name(bone),
		_muzzle.position, Figure.flash_kind(_lab, weapon_uri)])


## What a reload sounds like, from the model's own animation events.
##
## The .mdl has carried this the whole time and nothing played it. An AK's reload sequence
## names weapons/ak47_clipout.wav and weapons/ak47_clipin.wav, a USP names three including the
## slide release, and the M3 keeps its shell noises on start_reload and insert rather than on
## a sequence called reload — which is why the chain is walked instead of one name asked for.
func _load_reload_sounds(weapon: Dictionary) -> void:
	_reload_sounds.clear()
	_reload_times = PackedFloat32Array()
	var uri := ModelPreset.resolve(_lab.vfs, str(weapon.get("model", "")))
	if uri.is_empty():
		return

	# Walked in order, carrying the offset of each sequence, so a pump shotgun's shells land
	# at their own moment inside their own insert rather than all inside the first one.
	var offset := 0.0
	var seen := {}
	for sequence in _reload_chain:
		var name := str(sequence)
		for e in _lab.importer.list_sound_events(uri, name):
			var event: Dictionary = e
			var path := str(event["sound"])
			# The chain repeats "insert" once per shell, and so would its sound.
			var key := "%s@%.3f" % [path, offset + float(event["time"])]
			if seen.has(key):
				continue
			seen[key] = true
			var stream: AudioStream = SoundLoader.load_sound(_lab.vfs, path)
			if stream != null:
				_reload_sounds.append(stream)
				_reload_times.append(offset + float(event["time"]))
		if _view_anim != null and _view_anim.has_animation(name):
			offset += _view_anim.get_animation(name).length

	if _reload_sounds.is_empty() and not _reload_chain.is_empty():
		push_warning("[player] this weapon's reload names no sound its own model can resolve")


# ----------------------------------------------------------------------- input ----

func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventMouseMotion and Input.mouse_mode == Input.MOUSE_MODE_CAPTURED:
		rotate_y(-event.relative.x * LOOK)
		_sway = (_sway + event.relative * LOOK).limit_length(0.14)
		# Just short of straight up and down: at exactly 90 degrees the basis degenerates.
		_pitch = clampf(_pitch - event.relative.y * LOOK, -1.5, 1.5)
		_camera.rotation.x = _pitch

	elif event is InputEventMouseButton and event.pressed \
			and event.button_index == MOUSE_BUTTON_LEFT \
			and Input.mouse_mode != Input.MOUSE_MODE_CAPTURED:
		Input.mouse_mode = Input.MOUSE_MODE_CAPTURED

	elif event is InputEventKey and event.pressed and not event.echo:
		if event.keycode == KEY_R:
			_reload()
		elif event.keycode == KEY_E:
			# The use key: whatever button or door is within reach of the eye and in front
			# of it.
			if _lab != null and _lab.has_method("use_brush"):
				_lab.use_brush(_camera.global_position, -_camera.global_transform.basis.z)
		elif event.keycode == KEY_F2:
			if _lab != null and _lab.has_method("toggle_enhanced"):
				_lab.toggle_enhanced()
		elif event.keycode == KEY_G:
			_god = not _god
			if _god:
				health = 100
			print("[player] god mode %s" % ("on" if _god else "off"))
		elif event.keycode == KEY_V:
			_noclip = not _noclip
			for child in get_children():
				if child is CollisionShape3D:
					(child as CollisionShape3D).disabled = _noclip
			velocity = Vector3.ZERO
			print("[player] noclip %s" % ("on" if _noclip else "off"))
		elif event.keycode == KEY_F4:
			# Off to the next thing in the level that moves, flying, to look at it.
			if _lab != null and _lab.has_method("next_moving_place"):
				var where: Vector3 = _lab.next_moving_place()
				if where != Vector3.INF:
					_noclip = true
					for child in get_children():
						if child is CollisionShape3D:
							(child as CollisionShape3D).disabled = true
					velocity = Vector3.ZERO
					global_position = where + Vector3(0, 0.5, 3.0)
		elif event.keycode == KEY_ESCAPE:
			Input.mouse_mode = Input.MOUSE_MODE_VISIBLE


func _physics_process(delta: float) -> void:
	if health <= 0:
		return

	_play_reload_sounds()

	# The rounds arrive when the reload ends, not when it starts.
	if _refill_at > 0.0 and Time.get_ticks_msec() / 1000.0 >= _refill_at:
		_refill_at = 0.0
		var taken := mini(_clip_size - _clip, _reserve)
		_clip += taken
		_reserve -= taken

	var forward := 0.0
	var strafe := 0.0
	if Input.is_key_pressed(KEY_W): forward += 1.0
	if Input.is_key_pressed(KEY_S): forward -= 1.0
	if Input.is_key_pressed(KEY_D): strafe += 1.0
	if Input.is_key_pressed(KEY_A): strafe -= 1.0

	if _noclip:
		# Flown rather than walked, along the way the camera is pointing, so a ceiling is not
		# an obstacle to looking at what is above it.
		var fly := (_camera.global_transform.basis * Vector3(strafe, 0, -forward)).normalized()
		if Input.is_key_pressed(KEY_SPACE): fly += Vector3.UP
		if Input.is_key_pressed(KEY_CTRL): fly += Vector3.DOWN
		velocity = fly * _speed * 2.0
		move_and_slide()
		_bob_view(delta)
		return

	# Which of the level's volumes you are standing in decides how you move at all.
	_on_ladder = _find_ladder()
	var was_wet := _submerged
	_submerged = _depth_in_water()
	# Breaking the surface, either way, is a splash. pl_wade is what the engine plays for it.
	if (was_wet < 0.5) != (_submerged < 0.5):
		_splash()

	if _on_ladder:
		_climb(forward, strafe, delta)
		return

	var wishdir := (transform.basis * Vector3(strafe, 0, -forward)).normalized()
	var wishspeed := _speed
	if _submerged > 0.5:
		# PM_WaterMove: four fifths of what you would manage on land, and drag of its own.
		wishspeed *= WATER_SPEED
	_set_ducked(Input.is_key_pressed(KEY_CTRL))
	if _ducked:
		wishspeed *= CROUCH_FACTOR
	elif Input.is_key_pressed(KEY_SHIFT):
		wishspeed *= WALK_FACTOR

	# Quake's movement, which GoldSrc inherited and never replaced. Setting velocity straight
	# from the input gives a character that starts and stops instantly and cannot carry
	# momentum through a turn; the whole feel of these games is in these two functions.
	if _submerged > 1.5:
		# Swimming, which is not falling slowly. There is no gravity here at all: the vertical
		# movement is a wish like any other, and doing nothing wishes gently downward.
		_apply_friction(delta)
		_accelerate(wishdir, wishspeed, ACCELERATE, delta)

		var wish_up := -WATER_SINK
		if Input.is_key_pressed(KEY_SPACE):
			wish_up = wishspeed
		elif Input.is_key_pressed(KEY_CTRL):
			wish_up = -wishspeed
		elif forward != 0.0 or strafe != 0.0:
			# Swimming along looks where you look, so a dive is aiming down and pressing
			# forward. Nothing else in the engine makes a swim controllable.
			wish_up = -_camera.global_transform.basis.z.y * wishspeed * absf(forward)
		velocity.y = move_toward(velocity.y, wish_up, wishspeed * 4.0 * delta)
	elif is_on_floor():
		_apply_friction(delta)
		_accelerate(wishdir, wishspeed, ACCELERATE, delta)
		if Input.is_key_pressed(KEY_SPACE):
			velocity.y = JUMP
	else:
		_accelerate(wishdir, minf(wishspeed, MAX_AIR_SPEED), AIR_ACCELERATE, delta)
		velocity.y -= GRAVITY * delta

	# Held, not tapped. Most Counter-Strike weapons are automatic and the rate is governed by
	# the weapon's own cycle time, so tying a shot to the button going down turned an AK into
	# a semi-automatic. Which weapons are genuinely semi-automatic is in the game's code and
	# is not read yet; until it is, everything holds.
	if Input.is_mouse_button_pressed(MOUSE_BUTTON_LEFT) \
			and Input.mouse_mode == Input.MOUSE_MODE_CAPTURED:
		_shoot()

	# Up a stair rather than into it. Also for a moment after the last one, because between
	# two stairs the body is not always counted as standing.
	var stair_clock := Time.get_ticks_msec() / 1000.0
	var gain := Stairs.move(self, delta, is_on_floor() or stair_clock < _stepped_until)
	if gain > 0.0:
		_stair_lag = minf(_stair_lag + gain, Stairs.HEIGHT)
		_stepped_until = stair_clock + 0.15

	_drive_animation()
	_step_sound()
	_bob_view(delta)


## Let the next noise of a reload out when its turn comes.
func _play_reload_sounds() -> void:
	if _reload_queue >= _reload_sounds.size() or _reload_audio == null:
		return
	var elapsed := Time.get_ticks_msec() / 1000.0 - _reload_started
	if _reload_queue < _reload_times.size() and elapsed < _reload_times[_reload_queue]:
		return
	_reload_audio.stream = _reload_sounds[_reload_queue]
	_reload_audio.play()
	_reload_queue += 1


## Is the hull against one of the level's ladders, and which way does that ladder face?
##
## The hull and not the middle of it. A ladder's volume is the thickness of the rungs, a few
## inches deep on the face of a wall, and the middle of a body sixteen units wide never gets
## inside it: which is why no ladder in the level could be climbed.
func _find_ladder() -> bool:
	if _ladders.is_empty() or _hull == null:
		return false
	var capsule := _hull.shape as CapsuleShape3D
	if capsule == null:
		return false
	var middle := global_position + Vector3(0, _hull.position.y, 0)
	var half := Vector3(capsule.radius + LADDER_REACH, capsule.height * 0.5,
		capsule.radius + LADDER_REACH)
	var me := AABB(middle - half, half * 2.0)
	for box in _ladders:
		if not box.intersects(me):
			continue
		# Out of the wall: from the nearest point of the volume toward the body, level. Inside
		# it, along whichever way the volume is thinner, on the side the body is nearer.
		var near := Vector3(clampf(middle.x, box.position.x, box.end.x), middle.y,
			clampf(middle.z, box.position.z, box.end.z))
		var out := Vector3(middle.x - near.x, 0.0, middle.z - near.z)
		if out.length() < 0.001:
			var from_centre := middle - box.get_center()
			if box.size.x < box.size.z:
				out = Vector3(signf(from_centre.x), 0, 0)
			else:
				out = Vector3(0, 0, signf(from_centre.z))
		elif absf(out.x) > absf(out.z):
			out = Vector3(signf(out.x), 0, 0)
		else:
			out = Vector3(0, 0, signf(out.z))
		_ladder_out = out
		return true
	return false


## Climbing, which is not walking with a different name.
##
## Gravity is off, and the body goes along where it is looking and where it is strafing, at
## no more than MAX_CLIMB_SPEED. Whatever part of that is into or out of the ladder is turned
## up or down it instead: walking into a ladder climbs it, looking up at it and walking
## climbs it, and looking down from the top and walking forward comes down it. Jumping leaves
## it with a shove away, which is how anyone who has played these games gets off one.
func _climb(forward: float, strafe: float, delta: float) -> void:
	if Input.is_key_pressed(KEY_SPACE):
		# Off, and away from the wall you were on.
		_on_ladder = false
		velocity = _ladder_out * LADDER_JUMP
		move_and_slide()
		return

	var speed := CLIMB_SPEED
	if _ducked:
		speed *= CROUCH_FACTOR

	var eye := _camera.global_transform.basis
	var wish := ((-eye.z * forward) + (eye.x * strafe)) * speed
	if wish.length() < 0.001:
		# Hanging on.
		velocity = Vector3.ZERO
	else:
		var into := wish.dot(_ladder_out)
		var along := wish - _ladder_out * into
		velocity = along + Vector3.UP * -into
		# Standing at the foot of it and walking away is walking away, not climbing down
		# into the floor.
		if is_on_floor() and into > 0.0:
			velocity += _ladder_out * CLIMB_SPEED
	move_and_slide()

	if velocity.length() > 0.5:
		var now := Time.get_ticks_msec() / 1000.0
		if now >= _next_step:
			_next_step = now + LADDER_STEP
			_play_step("ladder", STEP_VOLUME_RUN)

	if Input.is_mouse_button_pressed(MOUSE_BUTTON_LEFT) \
			and Input.mouse_mode == Input.MOUSE_MODE_CAPTURED:
		_shoot()
	_drive_animation()
	_bob_view(delta)


# ------------------------------------------------------------------- the body ----

func _drive_animation() -> void:
	if _body_anim == null:
		return
	var wanted := AnimationSets.best(_stances, _stance())
	if wanted.is_empty() or not _body_anim.has_animation(wanted):
		return
	# The arms on top of the legs: aiming, or firing for a moment. The legs carry on from
	# where they were when only the arms change.
	var legs := wanted
	var keep := -1.0
	var firing := Time.get_ticks_msec() / 1000.0 < _shoot_until and not _shoot_move.is_empty()
	wanted = Figure.layered(_body_anim, _body, legs, _shoot_move if firing else _aim_move)
	if legs == _legs_now and _body_anim.current_animation != wanted and _body_anim.is_playing():
		keep = _body_anim.current_animation_position
	_legs_now = legs
	# `is_playing()` as well as the name. A stance that does not loop — jump is two seconds
	# and ends, die ends by design — leaves current_animation empty when it finishes, the
	# mixer stops writing to the skeleton, and the model snaps back to its bind pose. That is
	# the T-pose that appeared after a movement: not a missing animation, an ended one.
	if _body_anim.current_animation != wanted or not _body_anim.is_playing():
		_body_anim.play(wanted)
		if keep >= 0.0:
			_body_anim.seek(keep, true)


## Which stance matches what the body is doing.
##
## Counter-Strike keeps legs and torso apart: walk, run, crouch_idle and crouchrun drive the
## body while ref_aim_rifle and its kin drive the arms, and the engine blends them through
## gaitsequence. Only the legs are chosen here, because one AnimationPlayer plays one
## sequence — a limitation worth naming rather than leaving to be discovered.
func _stance() -> String:
	# A moment off the ground is not a jump. Walking over the ridges and stairs of a real map
	# loses floor contact for a frame at a time, and treating each of those as a jump started
	# a two second animation several times a second.
	if not is_on_floor():
		_airborne += get_physics_process_delta_time()
	else:
		_airborne = 0.0
	if _airborne > COYOTE:
		return "jump"
	var ground := Vector2(velocity.x, velocity.z).length()
	var moving := ground > _speed * 0.12
	if _ducked:
		return "crouch_walk" if moving else "crouch"
	if not moving:
		return "idle"
	return "walk" if ground < _speed * 0.55 else "run"


## Are you inside one of these volumes?
func _inside(boxes: Array[AABB]) -> bool:
	for box in boxes:
		if box.has_point(global_position):
			return true
	return false


## How deep, roughly: 0 dry, 1 feet wet, 2 swimming.
##
## PM_CheckWater samples three heights — just above the soles, half way up, and at the eye —
## and the answer changes what movement even means. Sampled the same way here.
func _depth_in_water() -> float:
	if _water.is_empty():
		return 0.0
	var feet := global_position + Vector3(0, _soles + 0.03, 0)
	var waist := global_position + Vector3(0, _soles + _stand_height * 0.5, 0)
	var eye := global_position + _eye_rest
	var depth := 0.0
	for box in _water:
		if box.has_point(feet):
			depth = maxf(depth, 1.0)
		if box.has_point(waist):
			depth = maxf(depth, 2.0)
		if box.has_point(eye):
			depth = maxf(depth, 3.0)
	return depth


## Shrink to the ducked hull, or grow back. The soles stay put and the head comes down.
func _set_ducked(on: bool) -> void:
	if on == _ducked or _hull == null:
		return
	var capsule := _hull.shape as CapsuleShape3D
	if capsule == null:
		return

	_ducked = on
	var wanted: float = _stand_height * (DUCK_HULL / STAND_HULL) if on else _stand_height
	capsule.height = maxf(wanted, capsule.radius * 2.0 + 0.01)
	_hull.position.y = _stand_y - (_stand_height - capsule.height) * 0.5
	_eye_rest = Vector3(0, _soles + (DUCK_EYE if on else EYE), 0)


# ---------------------------------------------------------------------- noise ----

## Footsteps, on the game's own rules: none in the air, silence while crouched or walking, and
## an interval measured in distance covered rather than in time.
func _step_sound() -> void:
	if _step_audio == null or not is_on_floor():
		return
	var ground := Vector2(velocity.x, velocity.z).length()
	# Below velwalk there is no step at all, which is what makes walking silent and is the
	# entire reason to hold Shift.
	if ground < VEL_WALK or _ducked:
		return

	# On a clock, not on a tape measure. 400 milliseconds walking and 300 running, with the
	# line between them at velrun, all out of PM_UpdateStepSound.
	var now := Time.get_ticks_msec() / 1000.0
	if now < _next_step:
		return
	var running := ground >= VEL_RUN
	_next_step = now + (STEP_INTERVAL_RUN if running else STEP_INTERVAL_WALK)
	_step_audio.volume_db = STEP_VOLUME_RUN if running else STEP_VOLUME_WALK

	# What is underfoot decides what it sounds like. Wading takes precedence over the floor,
	# because standing in a puddle on concrete is not a concrete noise.
	_play_step("wade" if _submerged >= 1.0 else _surface_underfoot(), _step_audio.volume_db)


## One of a surface's samples, at this loudness.
func _play_step(surface: String, loud: float) -> void:
	if _step_audio == null:
		return
	var samples: Array = _steps_by_surface.get(surface, [])
	if samples.is_empty():
		samples = _load_steps(surface)
	if samples.is_empty():
		return

	# Never the same one twice running, which is what the engine's own cache is for.
	var pick := randi() % samples.size()
	if samples.size() > 1 and pick == _last_step:
		pick = (pick + 1) % samples.size()
	_last_step = pick
	_step_audio.volume_db = loud
	_step_audio.stream = samples[pick]
	_step_audio.play()


## Which surface is under the feet, by the first letter of the texture there.
##
## GoldSrc keeps no material per face. It traces down, reads the name of the texture it hit and
## looks at its first character: 'M' is metal, 'G' a grate, 'D' dirt. A mapper changes how a
## floor sounds by renaming its texture, which is why this cannot be answered from geometry.
##
## Godot's ray gives a triangle index rather than a texture, so the bridge is the mesh: its
## surfaces are laid out one after another and each carries the name of the texture it was
## built from, so counting triangles says which surface a face belongs to.
func _surface_underfoot() -> String:
	var space := get_world_3d().direct_space_state
	var from := global_position + Vector3(0, _soles + 0.2, 0)
	var query := PhysicsRayQueryParameters3D.create(from, from + Vector3.DOWN * 1.2)
	query.collision_mask = SEEN_BY_RAYS
	query.exclude = [get_rid()]
	var hit := space.intersect_ray(query)
	if hit.is_empty() or not hit.has("face_index"):
		return _surface_now

	var body = hit["collider"]
	if body == null or body.get_parent() == null:
		return _surface_now
	var owner_mesh := body.get_parent() as MeshInstance3D
	if owner_mesh == null or owner_mesh.mesh == null:
		return _surface_now

	var mesh: Mesh = owner_mesh.mesh
	var table: Array = _face_table.get(mesh.get_rid(), [])
	if table.is_empty():
		# Where each surface starts, in triangles. Built once per mesh: a map has fifty or so
		# surfaces and this would otherwise be counted on every step.
		var running := 0
		for i in mesh.get_surface_count():
			var arrays := mesh.surface_get_arrays(i)
			var indices: PackedInt32Array = arrays[Mesh.ARRAY_INDEX]
			var count: int = int(indices.size() / 3.0) if indices.size() > 0 \
				else int((arrays[Mesh.ARRAY_VERTEX] as PackedVector3Array).size() / 3.0)
			running += count
			table.append(running)
		_face_table[mesh.get_rid()] = table

	var face := int(hit["face_index"])
	var surface := -1
	for i in table.size():
		if face < int(table[i]):
			surface = i
			break
	if surface < 0:
		return _surface_now

	var material := mesh.surface_get_material(surface)
	if material == null:
		return _surface_now
	var texture := str(material.resource_name)
	if texture.is_empty():
		return _surface_now

	_surface_now = SoundCatalogue.surface_of_texture(texture)
	if _surface_now.is_empty():
		# Everything the engine does not recognise falls to concrete, which is its own default.
		_surface_now = "step"
	return _surface_now


## Breaking the surface of water, in or out.
func _splash() -> void:
	if _step_audio == null:
		return
	var samples: Array = _steps_by_surface.get("wade", [])
	if samples.is_empty():
		samples = _load_steps("wade")
	if samples.is_empty():
		return
	_step_audio.volume_db = STEP_VOLUME_RUN
	_step_audio.stream = samples[randi() % samples.size()]
	_step_audio.play()


## Read one surface's samples, the first time it is walked on.
func _load_steps(surface: String) -> Array:
	var loaded: Array = []
	if _lab == null or _lab.vfs == null:
		return loaded
	for named in SoundCatalogue.footsteps(surface):
		var hit: Dictionary = _lab.vfs.find_files("sound/" + str(named),
			PackedStringArray(["wav"]), PackedStringArray(), PackedStringArray(), 1)
		var files: PackedStringArray = hit["files"]
		if files.is_empty():
			continue
		var stream: AudioStream = SoundLoader.load_sound(_lab.vfs, str(files[0]))
		if stream != null:
			loaded.append(stream)
	_steps_by_surface[surface] = loaded
	if loaded.is_empty():
		push_warning("[player] no footstep samples for '%s'" % surface)
	else:
		print("[player] %s: %d footstep sample(s)" % [surface, loaded.size()])
	return loaded


# -------------------------------------------------------------------- fighting ----

func _shoot() -> void:
	var now := Time.get_ticks_msec() / 1000.0
	if now < _busy_until or (_cycle > 0.0 and now < _next_shot):
		return
	_next_shot = now + maxf(_cycle, 0.05)

	if _clip_size > 0:
		if _clip <= 0:
			# Empty: with rounds left it reloads by itself, as the game does; with none it
			# clicks.
			if _reserve > 0:
				_reload()
			else:
				_next_shot = now + 0.25
				if _dry_click != null:
					_audio.stream = _dry_click
					_audio.play()
			return
		_clip -= 1

	if _gunshot != null:
		_audio.stream = _gunshot
		_audio.play()
	_shoot_until = now + 0.2
	Figure.flare(_muzzle)

	if _lab != null and _lab.get("enhanced") == true:
		if _flash == null:
			_flash = _lab.FlashScript.new()
			_camera.add_child(_flash)
			# Just ahead of the eye and no further, so it is never the other side of a wall
			# the player has their nose against.
			_flash.position = Vector3(0.1, -0.05, -0.75)
			_flash.light_the_hands()
		_flash.fire()
		# The kick of it: the hands jump back and up, and come to rest again by themselves.
		_sway.y += 0.035

	# One of the three at random, which is what the game does and most of why a rifle fired
	# ten times does not look like a loop.
	if _view_anim != null and not _attacks.is_empty():
		var pick: String = _attacks[randi() % _attacks.size()]
		if _view_anim.has_animation(pick):
			_view_anim.stop()
			_view_anim.play(pick)
			_rest_after()

	var from := _camera.global_position
	var query := PhysicsRayQueryParameters3D.create(
		from, from - _camera.global_transform.basis.z * RANGE)
	query.collision_mask = SEEN_BY_RAYS
	query.exclude = [get_rid()]
	var hit := get_world_3d().direct_space_state.intersect_ray(query)
	if hit.is_empty():
		return
	var struck = hit["collider"]
	var where: Vector3 = hit["position"]
	var facing: Vector3 = hit["normal"]
	var alive: bool = struck != null and struck.has_method("take_damage")

	var along := -_camera.global_transform.basis.z
	if alive:
		if struck.has_method("struck_at"):
			struck.struck_at(where, along)
		struck.take_damage(DAMAGE)
	elif _lab != null and _lab.has_method("hurt_brush"):
		# Not everything that can be shot is a character. A crate arrives as a static body
		# with no script on it, and what it belongs to is known by the brushes.
		_lab.hurt_brush(struck, float(DAMAGE))

	# Blood if it was somebody, a hole in the wall if it was not. The engine draws no decal on
	# a person for the same reason: UTIL_DecalTrace gives up on anything that is not level
	# geometry.
	if _lab != null and _lab.marks != null:
		if alive:
			# Along the shot, not against the face that was hit. A body is a rounded capsule
			# here, and the face's own direction off a glancing hit points sideways.
			_lab.marks.blood(where, along, DAMAGE, [struck.get_rid()])
		else:
			_lab.marks.bullet_hole(where, facing)


## Back to rest once whatever is playing has played out.
func _rest_after() -> void:
	if _view_anim != null and not _view_idle.is_empty():
		_view_anim.queue(_view_idle)


## Play the weapon's own reload, and refuse to fire until it has finished.
##
## Queued rather than played: a pump shotgun's reload is start_reload, four inserts and
## after_reload, and queue() is what makes them one action instead of the last one winning.
func _reload() -> void:
	if _view_anim == null or _reload_chain.is_empty():
		return
	var now := Time.get_ticks_msec() / 1000.0
	if now < _busy_until:
		return
	# A full weapon does not reload, and neither does one with nothing to put in it.
	if _clip_size > 0 and (_clip >= _clip_size or _reserve <= 0):
		return

	_view_anim.stop()
	var total := 0.0
	for i in _reload_chain.size():
		var sequence: String = _reload_chain[i]
		if not _view_anim.has_animation(sequence):
			continue
		if i == 0:
			_view_anim.play(sequence)
		else:
			_view_anim.queue(sequence)
		var clip := _view_anim.get_animation(sequence)
		if clip != null:
			total += clip.length
	_rest_after()
	_busy_until = now + total
	if _clip_size > 0:
		_refill_at = _busy_until

	# The noises, each at the moment the animator put it. The event carries the frame it
	# fires on and the sequence carries its frame rate, so this is exact rather than spread.
	if not _reload_sounds.is_empty():
		_reload_queue = 0
		_reload_started = now


## What the weapon holds, for the HUD: rounds in it and rounds carried. Empty for a knife.
func ammo_text() -> String:
	if _clip_size <= 0:
		return ""
	return "%d / %d" % [_clip, _reserve]


func take_damage(amount: int) -> void:
	if _god:
		return
	health = maxi(health - amount, 0)


# -------------------------------------------------------------------- the view ----

func _bob_view(delta: float) -> void:
	_bob_time += delta
	var cycle: float = fmod(_bob_time, BOB_CYCLE) / BOB_CYCLE
	# Not a plain sine: the upswing occupies the first half and the downswing the second,
	# mapped to different halves of pi, which is what gives a footstep weight instead of float.
	cycle = PI * cycle / BOB_UP if cycle < BOB_UP else PI + PI * (cycle - BOB_UP) / (1.0 - BOB_UP)

	var ground := Vector2(velocity.x, velocity.z).length()
	var bob := ground * BOB
	bob = clampf(bob * 0.3 + bob * 0.7 * sin(cycle), -7.0 * UNIT, 4.0 * UNIT)

	var side: float = velocity.dot(transform.basis.x)
	var roll: float = ROLL_ANGLE * clampf(side / ROLL_SPEED, -1.0, 1.0)

	_stair_lag = move_toward(_stair_lag, 0.0, maxf(150.0 * UNIT, _stair_lag * 12.0) * delta)
	_camera.position = _eye_rest + Vector3(0, bob - _stair_lag - _dip, 0)
	_camera.rotation.z = lerp_angle(_camera.rotation.z, deg_to_rad(-roll), 0.2)


## Draw the player's own figure, or go back to only its shadow.
func _show_body(on: bool) -> void:
	_body_shown = on
	for twin in _legs:
		twin.visible = on


## A weapon with some weight to it. None of this is in the game, which holds the view model
## rigidly in front of the eye and only bobs it; it is what the switch on F2 turns off.
##
## The hands follow the eyes a moment late, lean into a strafe, swing a little from side to
## side with each pair of steps, and come down with the body when it lands.
func _process(delta: float) -> void:
	if _view_pivot == null:
		return
	var grounded := is_on_floor()
	var on: bool = _lab != null and _lab.get("enhanced") == true
	if on != _body_shown:
		_show_body(on)

	if not grounded:
		_fall_speed = maxf(-velocity.y, 0.0)
	elif _was_falling and on:
		# Only a real drop: coming down a stair is not a landing.
		_dip = clampf((_fall_speed - 2.0) * 0.012, 0.0, 0.09)
	_was_falling = not grounded
	_dip = move_toward(_dip, 0.0, maxf(0.05, _dip * 7.0) * delta)
	_sway = _sway.lerp(Vector2.ZERO, clampf(delta * 9.0, 0.0, 1.0))

	if not on:
		_view_pivot.position = Vector3.ZERO
		_view_pivot.rotation = Vector3.ZERO
		return

	var pace := clampf(Vector2(velocity.x, velocity.z).length() / SPEED, 0.0, 1.2)
	if not grounded:
		pace = 0.0
	var side := clampf(velocity.dot(transform.basis.x) / SPEED, -1.0, 1.0)
	# Half the pace of the bob: one way on the left foot, the other on the right.
	var swing := sin(_bob_time / BOB_CYCLE * PI)
	var lift := clampf(velocity.y * 0.004, -0.02, 0.02)

	var want := Vector3(swing * 0.007 * pace - _sway.x * 0.05,
		-absf(swing) * 0.004 * pace - _dip * 0.6 - lift + _sway.y * 0.05,
		pace * 0.012)
	var turn := Vector3(_sway.y * 0.45, _sway.x * 0.45, -side * 0.045 + _sway.x * 0.3)
	var ease := clampf(delta * 14.0, 0.0, 1.0)
	_view_pivot.position = _view_pivot.position.lerp(want, ease)
	_view_pivot.rotation = _view_pivot.rotation.lerp(turn, ease)


## Add speed in the direction asked for, but only what is missing in that direction.
##
## The dot product is the whole trick. Speed already carried towards where the player points
## counts against the allowance, so running flat out gains nothing while turning into the
## movement leaves a gap that gets filled. In the air, where the allowance is 30 units per
## second, that gap reopens on every small turn of the mouse: that is air strafing, and it is
## not a bug anybody left in.
func _accelerate(wishdir: Vector3, wishspeed: float, accel: float, delta: float) -> void:
	if wishdir.length_squared() < 0.001:
		return
	var missing := wishspeed - velocity.dot(wishdir)
	if missing <= 0.0:
		return
	velocity += wishdir * minf(accel * wishspeed * delta, missing)


## Bleed speed off on the ground, harder once slow enough to be stopping. Below sv_stopspeed
## the drop is computed against that threshold rather than the real speed, so the last of the
## movement cuts off firmly instead of trailing away.
func _apply_friction(delta: float) -> void:
	var speed := velocity.length()
	if speed < 0.0001:
		return
	var control: float = STOP_SPEED if speed < STOP_SPEED else speed
	velocity *= maxf(speed - control * FRICTION * delta, 0.0) / speed
