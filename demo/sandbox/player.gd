extends CharacterBody3D

## First-person controller for the sandbox. Deliberately plain: this is here to prove the
## imported world can be walked through and shot at, not to be a movement system.
##
## The view sits where the model's own head is, so looking down shows the body that is really
## standing there: legs, arms, and the weapon in the hand holding it. Nothing is drawn twice
## for the benefit of the camera. That is the whole point of doing it this way, and it only
## works because the skeleton is animated rather than frozen.

const SPEED := 5.0
const GRAVITY := 22.0
const JUMP := 6.5
const LOOK_SENSITIVITY := 0.0022
const RANGE := 80.0
const DAMAGE := 34   # three hits, so a fight lasts long enough to be a fight

## While this is on, damage is reported but never applied. It exists so the level can be
## walked around and looked at without a fight interrupting, which is most of what anyone
## does with this the first few times.
const IMMORTAL := true

## Where the eyes are relative to the head bone, which sits at the base of the skull.
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


func setup(sandbox: Node3D, camera: Camera3D, skeleton: Skeleton3D = null,
		head_bone := -1, moves: Dictionary = {}) -> void:
	_sandbox = sandbox
	_camera = camera
	_skeleton = skeleton
	_head = head_bone
	_moves = moves
	if skeleton != null:
		_body_anim = skeleton.get_node_or_null("AnimationPlayer")


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
	_audio = AudioStreamPlayer3D.new()
	# The player's own weapon is at the camera, so at default settings 3D attenuation puts
	# it right on top of the listener. Pulled well down: this is a gunshot fired next to
	# someone's ear.
	_audio.volume_db = -14.0
	_audio.unit_size = 3.0
	add_child(_audio)
	Input.mouse_mode = Input.MOUSE_MODE_CAPTURED


## Take the weapon the sandbox built, with the sound and the animation that belong to it.
func arm(weapon: Dictionary) -> void:
	_gunshot = weapon.get("sound")
	_fire = str(weapon.get("fire", ""))
	_weapon_node = weapon.get("node")


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

	var direction := (transform.basis * Vector3(strafe, 0, -forward)).normalized()
	velocity.x = direction.x * SPEED
	velocity.z = direction.z * SPEED

	if is_on_floor():
		if Input.is_key_pressed(KEY_SPACE):
			velocity.y = JUMP
	else:
		velocity.y -= GRAVITY * delta

	move_and_slide()
	_drive_animation()


func _shoot() -> void:
	if _camera == null:
		return
	if _gunshot != null and _audio != null:
		_audio.stream = _gunshot
		_audio.play()

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
