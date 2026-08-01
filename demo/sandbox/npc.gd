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
var _moves := {}
var _playing := ""
var _weapon := ""
var _weapon_fire := ""
var _weapon_node: Node3D
var _cooldown := 0.0
var _audio: AudioStreamPlayer3D
var _gunshot: AudioStream


func setup(sandbox: Node3D, target: CharacterBody3D, moves: Dictionary,
		weapon: Dictionary) -> void:
	_sandbox = sandbox
	_target = target
	_moves = moves
	_weapon = str(weapon.get("name", ""))
	_gunshot = weapon.get("sound")
	_weapon_fire = str(weapon.get("fire", ""))
	_weapon_node = weapon.get("node")

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

	_cooldown -= delta
	var firing := can_see and distance < FIRE_RANGE and _cooldown <= 0.0
	if firing:
		_cooldown = FIRE_INTERVAL
		_shoot()

	# Firing reads over movement: an enemy shooting while it walks should look like it is
	# shooting, because that is the frame the player has to react to.
	if firing:
		_drive_animation("shoot")
	elif moving:
		_drive_animation("run" if distance > 12.0 else "walk")
	else:
		_drive_animation("idle")


## Play whichever sequence matches what the enemy is doing.
##
## Switched only when the role changes, never every frame: calling play() on the animation
## already running restarts it, which reads as a figure twitching on the spot.
func _drive_animation(role: String) -> void:
	if not _moves.has(role):
		# Nothing for this state, so hold whatever is running rather than snapping to a
		# rest pose. Not every model carries a sequence for everything.
		return
	var wanted := str(_moves[role])
	if wanted == _playing:
		return

	var skeleton := get_node_or_null("Skeleton3D")
	if skeleton == null:
		return
	var player: AnimationPlayer = skeleton.get_node_or_null("AnimationPlayer")
	if player == null or not player.has_animation(wanted):
		return

	_playing = wanted
	player.play(wanted)


func _shoot() -> void:
	if _gunshot != null and _audio != null:
		_audio.stream = _gunshot
		_audio.play()
	if not _weapon_fire.is_empty() and is_instance_valid(_weapon_node):
		var anim: AnimationPlayer = _weapon_node.get_node_or_null("AnimationPlayer")
		if anim != null and anim.has_animation(_weapon_fire):
			anim.stop()
			anim.play(_weapon_fire)
	if _target != null and _target.has_method("take_damage"):
		_target.take_damage(DAMAGE)


func take_damage(amount: int) -> void:
	health = maxi(0, health - amount)
	if health <= 0:
		_drive_animation("die")
		# Long enough for the death sequence to play. Freeing on the frame it dies means the
		# animation that was just imported is never seen once.
		set_physics_process(false)
		await get_tree().create_timer(2.0).timeout
		queue_free()
	if _sandbox != null:
		_sandbox.on_state_changed()
