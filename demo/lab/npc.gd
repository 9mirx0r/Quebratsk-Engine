extends CharacterBody3D

## An enemy built from a preset.
##
## The behaviour is the simplest thing that answers the question worth asking: walk toward the
## player and shoot when there is a clear line. No navigation mesh, so it will catch on
## scenery — that is a level-design problem rather than an import problem, and solving it
## would tell us nothing about the importer.

const AnimationSets := preload("res://addons/quebratsk_editor/animation_sets.gd")
const SoundLoader := preload("res://addons/quebratsk_editor/sound_loader.gd")
const ModelPreset := preload("res://addons/quebratsk_editor/model_preset.gd")

const SPEED := 3.0
const GRAVITY := 20.32          # sv_gravity 800, in metres
const SIGHT := 45.0
const FIRE_RANGE := 30.0
const FIRE_INTERVAL := 1.8
const DAMAGE := 7
const KEEP_AWAY := 6.0

## How long the feet may leave the ground before it counts as being in the air. Walking over
## the ridges of a real map loses contact for a frame at a time, and treating each of those as
## a jump starts a two second animation several times a second.
const COYOTE := 0.12

var health := 100

var _lab: Node3D
var _target: CharacterBody3D
var _stances := {}
var _body_anim: AnimationPlayer
var _weapon_anim: AnimationPlayer
var _attacks := PackedStringArray()
var _gunshot: AudioStream
var _audio: AudioStreamPlayer3D
var _cooldown := 0.0
var _dead := false
var _airborne := 0.0


func setup(lab: Node3D, target: CharacterBody3D, preset: Dictionary,
		weapon: Dictionary) -> void:
	_lab = lab
	_target = target
	_stances = preset.get("stances", {})
	name = str(preset.get("name", "enemy"))

	for child in get_children():
		if child is Skeleton3D:
			_body_anim = child.get_node_or_null("AnimationPlayer")

	_audio = AudioStreamPlayer3D.new()
	_audio.volume_db = -10.0
	_audio.unit_size = 8.0   # audible across a room, not across the level
	add_child(_audio)
	# Tracked, so somebody firing on the other side of a wall is heard through the wall.
	if lab.has_method("track_sound"):
		lab.track_sound(_audio)

	if not weapon.is_empty():
		_hold(weapon)

	# Stagger the first shot. Four enemies firing in unison from the instant the level loads
	# killed the first playtest in under three seconds, before anyone had read the HUD.
	_cooldown = randf_range(2.5, 2.5 + FIRE_INTERVAL)


## Put the weapon in the hand that holds it.
##
## On the right hand's bone, through a BoneAttachment3D, because a weapon parented to the body
## and positioned by hand ends up wherever the body's origin happens to be — which in GoldSrc
## is the centre of the hull rather than the hands, and put every rifle in the sky.
func _hold(weapon: Dictionary) -> void:
	for path in weapon.get("fire_sounds", []):
		var uri := ModelPreset.resolve(_lab.vfs, str(path))
		if uri.is_empty():
			continue
		_gunshot = SoundLoader.load_sound(_lab.vfs, uri)
		if _gunshot != null:
			break

	var skeleton: Skeleton3D = null
	for child in get_children():
		if child is Skeleton3D:
			skeleton = child as Skeleton3D
	if skeleton == null:
		return

	var hand := -1
	for b in skeleton.get_bone_count():
		var bone := skeleton.get_bone_name(b).to_lower()
		if bone.contains("r hand") or bone.contains("r_hand") or bone.contains("righthand"):
			hand = b
			break
	if hand < 0:
		# No hand to hang it from. Said out loud rather than hung off the root, which is how
		# rifles ended up floating above people's heads.
		print("[npc] %s has no right-hand bone, so it carries nothing" % name)
		return

	var model := ModelPreset.spawn(_lab.importer, _lab.vfs, weapon)
	if model == null:
		return
	for child in model.get_children():
		if child is StaticBody3D:
			model.remove_child(child)
			child.queue_free()

	var socket := BoneAttachment3D.new()
	socket.bone_idx = hand
	skeleton.add_child(socket)
	socket.add_child(model)

	_weapon_anim = model.get_node_or_null("AnimationPlayer")
	for child in model.get_children():
		if child is Skeleton3D and _weapon_anim == null:
			_weapon_anim = child.get_node_or_null("AnimationPlayer")
	if _weapon_anim != null:
		_attacks = AnimationSets.attacks(PackedStringArray(_weapon_anim.get_animation_list()))


func _physics_process(delta: float) -> void:
	if not is_on_floor():
		velocity.y -= GRAVITY * delta
		_airborne += delta
	else:
		velocity.y = 0.0
		_airborne = 0.0

	if _dead or _target == null or not is_instance_valid(_target):
		velocity.x = 0.0
		velocity.z = 0.0
		move_and_slide()
		return

	var to_target := _target.global_position - global_position
	var flat := Vector2(to_target.x, to_target.z)
	var distance := flat.length()

	var walking := false
	if distance < SIGHT and distance > KEEP_AWAY:
		var step := flat.normalized() * SPEED
		velocity.x = step.x
		velocity.y = velocity.y
		velocity.z = step.y
		walking = true
	else:
		velocity.x = 0.0
		velocity.z = 0.0

	# Face the player. atan2(-x, -z) rather than atan2(x, z): a model's forward is -Z in
	# Godot, and getting that backwards had every enemy walking toward you sideways.
	if distance > 0.01:
		rotation.y = atan2(-to_target.x, -to_target.z)

	_cooldown -= delta
	if distance < FIRE_RANGE and _cooldown <= 0.0:
		_cooldown = FIRE_INTERVAL
		_fire()

	_play(_stance(walking))
	move_and_slide()


func _stance(walking: bool) -> String:
	if _dead:
		return "die"
	if _airborne > COYOTE:
		return "jump"
	return "walk" if walking else "idle"


func _play(role: String) -> void:
	if _body_anim == null:
		return
	var wanted := AnimationSets.best(_stances, role)
	if wanted.is_empty() or not _body_anim.has_animation(wanted):
		return
	# `is_playing()` as well as the name. A stance that does not loop — jump is two seconds
	# and ends, die ends by design — leaves current_animation empty when it finishes, the
	# mixer stops writing to the skeleton, and the model snaps back to its bind pose. That is
	# the T-pose that appeared after a movement: not a missing animation, an ended one.
	if _body_anim.current_animation != wanted or not _body_anim.is_playing():
		_body_anim.play(wanted)


func _fire() -> void:
	if _gunshot != null:
		_audio.stream = _gunshot
		_audio.play()
	if _weapon_anim != null and not _attacks.is_empty():
		var pick: String = _attacks[randi() % _attacks.size()]
		if _weapon_anim.has_animation(pick):
			_weapon_anim.stop()
			_weapon_anim.play(pick)

	if _target != null and is_instance_valid(_target) and _target.has_method("take_damage"):
		_target.take_damage(DAMAGE)


func take_damage(amount: int) -> void:
	health = maxi(health - amount, 0)
	if health > 0 or _dead:
		return
	_dead = true
	# GoldSrc has no ragdolls. Health reaching zero raises deadflag, the hull shrinks to a
	# volume on the floor, bullets pass through, and a death animation freezes on its last
	# frame. A corpse reacts to nothing. Ragdolls here would be a modernisation, not a
	# restoration, and it is worth being clear about which.
	_play("die")
