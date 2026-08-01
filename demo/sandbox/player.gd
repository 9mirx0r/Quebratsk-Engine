extends CharacterBody3D

## First-person controller for the sandbox. Deliberately plain: this is here to prove the
## imported world can be walked through and shot at, not to be a movement system.
##
## The view sits where the model's own head is, so looking down shows the body that is really
## standing there: legs, arms, and the weapon in the hand holding it. Nothing is drawn twice
## for the benefit of the camera. That is the whole point of doing it this way, and it only
## works because the skeleton is animated rather than frozen.

## The engine's own numbers, converted once, instead of values that felt about right.
##
## A Hammer unit is an inch, so everything here is the GoldSrc constant times 0.0254. They
## were 5.0, 22.0 and 6.5 before, and none of those came from anywhere.
const UNIT := 0.0254

## sv_maxspeed is 320 in Half-Life, but Counter-Strike sets it high on the server and lets the
## weapon in hand decide: 250 for a knife or pistol, around 220 for a rifle, 210 for an AWP
## and 150 with it scoped. 250 is the unencumbered figure.
const SPEED := 250.0 * UNIT          # 6.35 m/s

## sv_gravity 800.
const GRAVITY := 800.0 * UNIT        # 20.32 m/s^2

## Not a number somebody liked. Valve built Half-Life around clearing a 45 unit crate, and the
## jump velocity is what that requires: sqrt(2 * 800 * 45) = 268.3 units per second.
const JUMP := 268.328 * UNIT         # 6.82 m/s

## Walking and crouching are fractions of whatever the weapon allows, not constants of their
## own: 0.52 and 0.333 in Counter-Strike.
const WALK_FACTOR := 0.52
const CROUCH_FACTOR := 0.333

## sv_stepsize 18: how high a ledge is climbed without jumping.
const STEP_HEIGHT := 18.0 * UNIT     # 0.457 m

## sv_accelerate, sv_airaccelerate, sv_friction and sv_stopspeed, at Counter-Strike's values.
const ACCELERATE := 5.0
const AIR_ACCELERATE := 10.0
const FRICTION := 4.0
const STOP_SPEED := 75.0 * UNIT

## The cap that makes air control a skill rather than free speed. Acceleration in the air is
## computed against 30 units per second no matter how fast the player wants to go, so turning
## into the movement grants a sliver at a time. Bunny hopping and air strafing come out of
## this one clamp; without it the same code hands out unlimited speed on the first frame.
const MAX_AIR_SPEED := 30.0 * UNIT

## View bobbing, from V_CalcBob() in cl_dll/view.cpp. The camera's rise and fall while walking
## is code, not an animation of the head: GoldSrc has no bone driving it and the amplitude is
## taken straight from how fast the body is moving across the ground.
const BOB := 0.01           # cl_bob
const BOB_CYCLE := 0.8      # cl_bobcycle, seconds for one full step
const BOB_UP := 0.5         # cl_bobup, where in the cycle the upswing ends
const BOB_MAX := 4.0 * UNIT
const BOB_MIN := -7.0 * UNIT

## V_CalcRoll(): the view leans into a strafe. Small, and its absence is one of those things
## that reads as stiffness without being nameable.
const ROLL_ANGLE := 2.0     # cl_rollangle, degrees
const ROLL_SPEED := 200.0 * UNIT
const LOOK_SENSITIVITY := 0.0022
const RANGE := 80.0
const DAMAGE := 34   # three hits, so a fight lasts long enough to be a fight

## While this is on, damage is reported but never applied. It exists so the level can be
## walked around and looked at without a fight interrupting, which is most of what anyone
## does with this the first few times.
const IMMORTAL := true

const WeaponManifest := preload("res://addons/quebratsk_editor/weapon_manifest.gd")

## Where the eyes are relative to the head bone, which sits at the base of the skull.
## VEC_VIEW puts the eye 64 units above the soles, which is 8 units below the top of a 72
## unit hull rather than at the crown. Measured from the head bone rather than from the feet,
## because that is what the camera follows here.
const EYE_HEIGHT := 64.0 * UNIT      # 1.626 m
const EYE_RAISE := 0.09
const EYE_FORWARD := 0.07

var health := 100

var _sandbox: Node3D
var _camera: Camera3D
var _skeleton: Skeleton3D
var _head := -1
var _pitch := 0.0
var _gunshot: AudioStream
var _audio: AudioStreamPlayer3D
var _fire := ""
var _weapon_node: Node3D
var _moves: Dictionary = {}
var _body_anim: AnimationPlayer
var _speed := SPEED
var _cycle := 0.0
var _next_shot := 0.0
var _bob_time := 0.0
var _eye_rest := Vector3.ZERO
var _complained := false


func setup(sandbox: Node3D, camera: Camera3D, skeleton: Skeleton3D = null,
		head_bone := -1, moves: Dictionary = {}) -> void:
	_sandbox = sandbox
	_camera = camera
	_skeleton = skeleton
	_head = head_bone
	_moves = moves
	if skeleton != null:
		_body_anim = skeleton.get_node_or_null("AnimationPlayer")
	# VEC_VIEW, not the head bone. GoldSrc puts the eye at a fixed 64 units above the soles and
	# adds V_CalcBob; it does not follow a bone, and following one hands the view the whole
	# swing of a third-person walk cycle, which is both wrong and unpleasant. Having the real
	# constant is what made this worth changing.
	if camera != null:
		camera.position = Vector3(0, EYE_HEIGHT, 0)
		_eye_rest = camera.position
	_head = -1


func _ready() -> void:
	_audio = AudioStreamPlayer3D.new()
	# The player's own weapon is at the camera, so at default settings 3D attenuation puts
	# it right on top of the listener. Pulled well down: this is a gunshot fired next to
	# someone's ear.
	_audio.volume_db = -14.0
	_audio.unit_size = 3.0
	add_child(_audio)
	Input.mouse_mode = Input.MOUSE_MODE_CAPTURED


## Play the stance that matches what the body is doing.
##
## The enemies have done this since they were written and the player never did, so the figure
## casting the shadow slid around frozen in one pose. It shows in a shadow before it shows
## anywhere else, which is how it was spotted.
func _drive_animation() -> void:
	if _body_anim == null:
		return
	var speed := Vector2(velocity.x, velocity.z).length()
	var role := "idle"
	if speed > SPEED * 0.6:
		role = "run"
	elif speed > 0.4:
		role = "walk"

	var wanted := str(_moves.get(role, ""))
	# A model with no walk still has an idle, and standing still beats not moving at all.
	if wanted.is_empty():
		wanted = str(_moves.get("idle", ""))
	if wanted.is_empty() or not _body_anim.has_animation(wanted):
		return
	if _body_anim.current_animation != wanted:
		_body_anim.play(wanted)


## Take the weapon the sandbox built, with the sound and the animation that belong to it.
func arm(weapon: Dictionary) -> void:
	_gunshot = weapon.get("sound")
	_fire = str(weapon.get("fire", ""))
	_weapon_node = weapon.get("node")

	# What you carry decides how fast you move, which is Counter-Strike's rule rather than
	# Half-Life's: a knife runs at 250 units per second, an AK at 221, an AWP at 210. It is
	# most of why picking up the sniper rifle feels like a decision, and the number comes from
	# the game rather than from anything guessable about the model.
	# How fast it can be fired. Without this every weapon empties as fast as the button is
	# pressed, and an AWP behaves like a machine gun.
	_cycle = WeaponManifest.cycle_time(str(weapon.get("name", "")))
	_next_shot = 0.0

	var declared: float = WeaponManifest.max_speed(str(weapon.get("name", "")))
	if declared > 0.0:
		_speed = declared
		print("[player] %s allows %.2f m/s" % [str(weapon.get("name", "")), _speed])
	else:
		_speed = SPEED


## Put the view where the head is, every frame, after the animation has moved it.
##
## The offset is taken along the body's facing rather than the camera's. Along the camera's,
## looking at the floor would walk the viewpoint forward and down into the model's own chest,
## which is the one direction a player in a first-person game looks deliberately.
func _process(_delta: float) -> void:
	if _camera == null or _skeleton == null or _head < 0:
		return

	var head := _skeleton.global_transform * _skeleton.get_bone_global_pose(_head)
	var facing := -global_transform.basis.z
	_camera.global_position = head.origin + Vector3.UP * EYE_RAISE + facing * EYE_FORWARD


func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventMouseMotion and Input.mouse_mode == Input.MOUSE_MODE_CAPTURED:
		rotate_y(-event.relative.x * LOOK_SENSITIVITY)
		# Clamped just short of straight up and down: at exactly 90 degrees the camera
		# basis degenerates and the view rolls.
		_pitch = clampf(_pitch - event.relative.y * LOOK_SENSITIVITY, -1.5, 1.5)
		if _camera != null:
			_camera.rotation.x = _pitch

	elif event is InputEventMouseButton and event.pressed \
			and event.button_index == MOUSE_BUTTON_LEFT:
		if Input.mouse_mode == Input.MOUSE_MODE_CAPTURED:
			_shoot()
		else:
			Input.mouse_mode = Input.MOUSE_MODE_CAPTURED

	elif event is InputEventKey and event.pressed and event.keycode == KEY_ESCAPE:
		Input.mouse_mode = Input.MOUSE_MODE_VISIBLE


func _physics_process(delta: float) -> void:
	if health <= 0:
		return

	var forward := 0.0
	var strafe := 0.0
	if Input.is_key_pressed(KEY_W): forward += 1.0
	if Input.is_key_pressed(KEY_S): forward -= 1.0
	if Input.is_key_pressed(KEY_D): strafe += 1.0
	if Input.is_key_pressed(KEY_A): strafe -= 1.0

	var wishdir := (transform.basis * Vector3(strafe, 0, -forward)).normalized()
	var wishspeed := _speed
	if Input.is_key_pressed(KEY_SHIFT):
		wishspeed *= WALK_FACTOR
	elif Input.is_key_pressed(KEY_CTRL):
		wishspeed *= CROUCH_FACTOR

	# Quake's movement, which GoldSrc inherited and never replaced. Setting velocity straight
	# from the input direction, which is what this did, gives a character that starts and stops
	# instantly and cannot carry momentum through a turn. The whole feel of these games is in
	# the two functions below.
	if is_on_floor():
		_apply_friction(delta)
		_accelerate(wishdir, wishspeed, ACCELERATE, delta)
		if Input.is_key_pressed(KEY_SPACE):
			velocity.y = JUMP
	else:
		_accelerate(wishdir, minf(wishspeed, MAX_AIR_SPEED), AIR_ACCELERATE, delta)
		velocity.y -= GRAVITY * delta

	move_and_slide()
	_drive_animation()
	_bob_view(delta)


## Rise, fall and lean the view the way walking does.
##
## Two separate effects that the engine computes rather than animates. The cycle is not a plain
## sine: the upswing occupies the first half and the downswing the second, mapped to different
## halves of pi, which is what gives a footstep its weight instead of a float. Amplitude comes
## from ground speed alone, so standing still is perfectly still.
##
func _bob_view(delta: float) -> void:
	if _camera == null:
		return

	_bob_time += delta
	var cycle: float = fmod(_bob_time, BOB_CYCLE) / BOB_CYCLE
	if cycle < BOB_UP:
		cycle = PI * cycle / BOB_UP
	else:
		cycle = PI + PI * (cycle - BOB_UP) / (1.0 - BOB_UP)

	var ground := Vector2(velocity.x, velocity.z).length()
	var bob := ground * BOB
	# Never all the way to zero at the turn: a third of it is constant and the rest swings.
	bob = bob * 0.3 + bob * 0.7 * sin(cycle)
	bob = clampf(bob, BOB_MIN, BOB_MAX)

	# Leaning into a strafe, from the sideways component of the movement.
	var side: float = velocity.dot(transform.basis.x)
	var roll: float = ROLL_ANGLE * clampf(side / ROLL_SPEED, -1.0, 1.0)

	_camera.position = _eye_rest + Vector3(0, bob, 0)
	_camera.rotation.z = lerp_angle(_camera.rotation.z, deg_to_rad(-roll), 0.2)


## Add speed in the direction asked for, but only as much as is missing in that direction.
##
## The dot product is the whole trick. Speed already being carried towards where the player is
## pointing counts against the allowance, so running straight ahead at full tilt gains nothing,
## while turning into the movement leaves a gap and the gap gets filled. In the air, where the
## allowance is capped at 30 units per second, that gap reopens on every small turn of the
## mouse, which is where air strafing comes from. It is not a bug anybody left in.
func _accelerate(wishdir: Vector3, wishspeed: float, accel: float, delta: float) -> void:
	if wishdir.length_squared() < 0.001:
		return
	var current := velocity.dot(wishdir)
	var missing := wishspeed - current
	if missing <= 0.0:
		return
	velocity += wishdir * minf(accel * wishspeed * delta, missing)


## Bleed speed off on the ground, harder once slow enough to be stopping.
##
## Below sv_stopspeed the drop is computed against that threshold rather than against the
## actual speed, so the last of the movement is scrubbed briskly instead of trailing off.
func _apply_friction(delta: float) -> void:
	var speed := velocity.length()
	if speed < 0.0001:
		return
	var control: float = STOP_SPEED if speed < STOP_SPEED else speed
	var kept: float = maxf(speed - control * FRICTION * delta, 0.0) / speed
	velocity *= kept


func _shoot() -> void:
	if _camera == null:
		return
	var now := Time.get_ticks_msec() / 1000.0
	if _cycle > 0.0 and now < _next_shot:
		return
	_next_shot = now + _cycle
	if _gunshot != null and _audio != null:
		_audio.stream = _gunshot
		_audio.play()
	elif not _complained:
		# Firing in silence used to be indistinguishable from firing correctly, and that is
		# how a missing audio player survived: nothing anywhere said the shot made no noise.
		_complained = true
		if _audio == null:
			push_warning("[player] no audio player, so nothing this weapon does will be heard")
		else:
			push_warning("[player] no firing sound was resolved for this weapon")

	# The weapon's own firing animation, restarted on every shot: unlike a walk cycle, this
	# one is meant to play from the top each time the trigger goes.
	if not _fire.is_empty() and is_instance_valid(_weapon_node):
		var anim: AnimationPlayer = _weapon_node.get_node_or_null("AnimationPlayer")
		if anim != null and anim.has_animation(_fire):
			anim.stop()
			anim.play(_fire)

	var from := _camera.global_position
	var to := from - _camera.global_transform.basis.z * RANGE
	var query := PhysicsRayQueryParameters3D.create(from, to)
	query.exclude = [get_rid()]
	var hit := get_world_3d().direct_space_state.intersect_ray(query)
	if hit.is_empty():
		return

	var struck = hit["collider"]
	if struck != null and struck.has_method("take_damage"):
		struck.take_damage(DAMAGE)


func take_damage(amount: int) -> void:
	if IMMORTAL:
		return
	health = maxi(0, health - amount)
	if _sandbox != null:
		_sandbox.on_state_changed()
	if health <= 0:
		Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
