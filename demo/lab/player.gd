extends CharacterBody3D

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
const STEP_VOLUME_WALK := -14.0    # 0.2 and 0.5 in the engine's linear scale
const STEP_VOLUME_RUN := -6.0

## MAX_CLIMB_SPEED, and the shove a jump gives you off a ladder.
const CLIMB_SPEED := 200.0 * UNIT
const LADDER_JUMP := 270.0 * UNIT

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
var _airborne := 0.0

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
var _stand_height := 0.0
var _stand_y := 0.0
var _ducked := false

var _view_anim: AnimationPlayer
var _attacks := PackedStringArray()
var _reload_chain := PackedStringArray()

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
			_body_anim = child.get_node_or_null("AnimationPlayer")

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


## Hold the weapon: its own model in front of the camera, and what the game says it does.
func _arm(weapon: Dictionary) -> void:
	_cycle = float(weapon.get("cycle_time", 0.0))
	var declared := float(weapon.get("max_speed", 0.0))
	_speed = declared if declared > 0.0 else SPEED

	var model := ModelPreset.spawn(_lab.importer, _lab.vfs, weapon)
	if model == null:
		push_warning("[player] the weapon preset could not be built")
		return

	# A view model has no business colliding with anything.
	for child in model.get_children():
		if child is StaticBody3D:
			model.remove_child(child)
			child.queue_free()
	model.position = Vector3(0.0, -0.06, -0.08)
	_camera.add_child(model)

	_view_anim = model.get_node_or_null("AnimationPlayer")
	if _view_anim == null:
		for child in model.get_children():
			if child is Skeleton3D:
				_view_anim = child.get_node_or_null("AnimationPlayer")
	if _view_anim != null:
		var has := PackedStringArray(_view_anim.get_animation_list())
		_attacks = AnimationSets.attacks(has)
		_reload_chain = AnimationSets.reload_chain(has)

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
		elif event.keycode == KEY_ESCAPE:
			Input.mouse_mode = Input.MOUSE_MODE_VISIBLE


func _physics_process(delta: float) -> void:
	if health <= 0:
		return

	_play_reload_sounds()

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
	_on_ladder = _inside(_ladders)
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

	move_and_slide()
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


## Climbing, which is not walking with a different name.
##
## PM_LadderMove turns off gravity, flies the body along where you are looking, and caps you at
## MAX_CLIMB_SPEED. Jumping leaves the ladder with a shove away from it, which is how anyone
## who has played these games gets off one.
func _climb(forward: float, strafe: float, delta: float) -> void:
	if Input.is_key_pressed(KEY_SPACE):
		# Off, and away from the wall you were on.
		_on_ladder = false
		velocity = -_camera.global_transform.basis.z * LADDER_JUMP * 0.5 + Vector3.UP * JUMP
		move_and_slide()
		return

	var speed := CLIMB_SPEED
	if _ducked:
		speed *= CROUCH_FACTOR

	# Along the view rather than along the floor: looking up and pressing forward climbs, which
	# is the whole of how a GoldSrc ladder is used.
	var eye := _camera.global_transform.basis
	velocity = ((-eye.z * forward) + (eye.x * strafe)) * speed
	move_and_slide()
	_drive_animation()
	_bob_view(delta)


# ------------------------------------------------------------------- the body ----

func _drive_animation() -> void:
	if _body_anim == null:
		return
	var wanted := AnimationSets.best(_stances, _stance())
	if wanted.is_empty() or not _body_anim.has_animation(wanted):
		return
	# `is_playing()` as well as the name. A stance that does not loop — jump is two seconds
	# and ends, die ends by design — leaves current_animation empty when it finishes, the
	# mixer stops writing to the skeleton, and the model snaps back to its bind pose. That is
	# the T-pose that appeared after a movement: not a missing animation, an ended one.
	if _body_anim.current_animation != wanted or not _body_anim.is_playing():
		_body_anim.play(wanted)


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
	var surface := "wade" if _submerged >= 1.0 else _surface_underfoot()
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

	if _gunshot != null:
		_audio.stream = _gunshot
		_audio.play()

	# One of the three at random, which is what the game does and most of why a rifle fired
	# ten times does not look like a loop.
	if _view_anim != null and not _attacks.is_empty():
		var pick: String = _attacks[randi() % _attacks.size()]
		if _view_anim.has_animation(pick):
			_view_anim.stop()
			_view_anim.play(pick)

	var from := _camera.global_position
	var query := PhysicsRayQueryParameters3D.create(
		from, from - _camera.global_transform.basis.z * RANGE)
	query.exclude = [get_rid()]
	var hit := get_world_3d().direct_space_state.intersect_ray(query)
	if hit.is_empty():
		return
	var struck = hit["collider"]
	var where: Vector3 = hit["position"]
	var facing: Vector3 = hit["normal"]
	var alive: bool = struck != null and struck.has_method("take_damage")

	if alive:
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
			_lab.marks.blood(where, -facing, DAMAGE)
		else:
			_lab.marks.bullet_hole(where, facing)


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
	_busy_until = now + total

	# The noises, each at the moment the animator put it. The event carries the frame it
	# fires on and the sequence carries its frame rate, so this is exact rather than spread.
	if not _reload_sounds.is_empty():
		_reload_queue = 0
		_reload_started = now


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

	_camera.position = _eye_rest + Vector3(0, bob, 0)
	_camera.rotation.z = lerp_angle(_camera.rotation.z, deg_to_rad(-roll), 0.2)


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
