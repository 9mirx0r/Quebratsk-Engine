extends CharacterBody3D

## An enemy built from an imported model.
##
## The script is attached to the CharacterBody3D that load_character() returns, so the
## skeleton, its mesh and its AnimationPlayer are already children and nothing here has to
## know how the model was assembled.
##
## The behaviour is the simplest thing that answers the question worth asking: walk toward
## the player, and shoot when there is a clear line. No navigation mesh, so it will get
## caught on scenery; that is a level-design problem rather than an import problem, and
## solving it would not tell us anything new about the importer.

const SPEED := 3.0
const GRAVITY := 22.0
const SIGHT := 45.0
const FIRE_RANGE := 30.0
const FIRE_INTERVAL := 1.8
const DAMAGE := 7
const STOP_AT := 6.0    # keeps its distance rather than walking into your face

var health := 100

var _sandbox: Node3D
var _target: CharacterBody3D
var _walk := ""
var _weapon := ""
var _cooldown := 0.0
var _audio: AudioStreamPlayer3D
var _gunshot: AudioStream


func setup(sandbox: Node3D, target: CharacterBody3D, walk_animation: String,
		weapon: String) -> void:
	_sandbox = sandbox
	_target = target
	_walk = walk_animation
	_weapon = weapon
	_gunshot = sandbox.find_gunshot()

	_audio = AudioStreamPlayer3D.new()
	_audio.volume_db = -10.0
	_audio.unit_size = 8.0   # audible across a room, not across the level
	add_child(_audio)

	# Stagger the first shot, and give the player a moment before anyone opens up. Four
	# enemies firing in unison from the instant the level loads killed the first playtest in
	# under three seconds, before they had finished reading the HUD.
	_cooldown = randf_range(2.5, 2.5 + FIRE_INTERVAL)


func _player_visible() -> bool:
	if _target == null:
		return false
	var eye := global_position + Vector3(0, 1.4, 0)
	var theirs := _target.global_position + Vector3(0, 1.4, 0)
	var query := PhysicsRayQueryParameters3D.create(eye, theirs)
	query.exclude = [get_rid()]
	var hit := get_world_3d().direct_space_state.intersect_ray(query)
	return not hit.is_empty() and hit["collider"] == _target


func _physics_process(delta: float) -> void:
	if health <= 0 or _target == null or _target.health <= 0:
		velocity = Vector3.ZERO
		move_and_slide()
		return

	var to_target := _target.global_position - global_position
	var distance := Vector2(to_target.x, to_target.z).length()
	var can_see := distance < SIGHT and _player_visible()

	# Face the player whenever it knows where they are, so it shoots the way it is looking.
	if can_see and distance > 0.1:
		var yaw := atan2(to_target.x, to_target.z)
		rotation.y = lerp_angle(rotation.y, yaw, 0.15)

	var moving := false
	if can_see and distance > STOP_AT:
		var direction := Vector3(to_target.x, 0, to_target.z).normalized()
		velocity.x = direction.x * SPEED
		velocity.z = direction.z * SPEED
		moving = true
	else:
		velocity.x = 0.0
		velocity.z = 0.0

	if not is_on_floor():
		velocity.y -= GRAVITY * delta
	move_and_slide()
	_drive_animation(moving)

	_cooldown -= delta
	if can_see and distance < FIRE_RANGE and _cooldown <= 0.0:
		_cooldown = FIRE_INTERVAL
		_shoot()


## Play the walk sequence while moving and hold still otherwise. The model may have come in
## with no animation at all, in which case there is simply nothing to drive.
func _drive_animation(moving: bool) -> void:
	if _walk.is_empty():
		return
	var skeleton := get_node_or_null("Skeleton3D")
	if skeleton == null:
		return
	var player: AnimationPlayer = skeleton.get_node_or_null("AnimationPlayer")
	if player == null:
		return
	if moving and not player.is_playing():
		player.play(_walk)
	elif not moving and player.is_playing():
		player.pause()


func _shoot() -> void:
	if _gunshot != null and _audio != null:
		_audio.stream = _gunshot
		_audio.play()
	if _target != null and _target.has_method("take_damage"):
		_target.take_damage(DAMAGE)


func take_damage(amount: int) -> void:
	health = maxi(0, health - amount)
	if health <= 0:
		queue_free()
	if _sandbox != null:
		_sandbox.on_state_changed()
