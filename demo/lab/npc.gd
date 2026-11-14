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
const Stairs := preload("res://lab/stairs.gd")
const SoundCatalogue := preload("res://addons/quebratsk_editor/sound_catalogue.gd")
const Figure := preload("res://lab/figure.gd")

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

## What a Counter-Strike player model holds for being hurt, beside walking about: a flinch for
## the head and one for the body, and several ways to die. The names are the model's own
## sequences, read along with the stances.
const REACTIONS: Array[String] = ["head_flinch", "gut_flinch",
	"death1", "death2", "death3", "head", "gutshot"]
## The ordinary deaths, one picked at random, and the ones kept for where the bullet went.
const DEATHS := ["death1", "death2", "death3", "gutshot"]
const DEATH_BY_HEAD := "head"

## What the game plays when a player is hit, out of sound/player: a body hit, a head hit, and
## the death cries. Picked at random within each.
const HIT_SOUNDS := ["player/bhit_flesh-1.wav", "player/bhit_flesh-2.wav",
	"player/bhit_flesh-3.wav"]
const HEAD_SOUNDS := ["player/headshot1.wav", "player/headshot2.wav", "player/headshot3.wav"]
const DEATH_SOUNDS := ["player/die1.wav", "player/die2.wav", "player/die3.wav",
	"player/death6.wav"]
## The grunt of somebody hurt, which the game keeps beside those and plays for a fall. Here it
## goes with a bullet too: a thud alone, forty metres off under gunfire, is not heard at all.
const PAIN_SOUNDS := ["player/pl_pain2.wav", "player/pl_pain4.wav", "player/pl_pain5.wav",
	"player/pl_pain6.wav", "player/pl_pain7.wav"]
## How loud being hit is, and how far it carries. A choice, made by ear against the gunshots:
## at the level the game's own files sit at they are lost under the rifle that caused them.
const CRY_LOUD := 3.0
const CRY_CARRIES := 22.0

## How far below the top of the body counts as the head, in metres. A choice: the game asks
## the model's own hitboxes, and here a body is one capsule.
const HEAD_HIGH := 0.28
## How long a flinch holds the body before it goes back to walking, at most, in seconds.
const FLINCH_AT_MOST := 0.45
## A hit slows whoever takes it for a moment, which is most of what makes shooting somebody
## feel like it landed. How much and how long are a choice.
const SLOWED := 0.45
const SLOWED_FOR := 0.5

var health := 100

var _lab: Node3D
var _target: CharacterBody3D
var _stances := {}
var _body_anim: AnimationPlayer
var _skeleton: Skeleton3D
## What the arms do over whatever the legs are doing: holding the weapon up, and firing it.
var _aim_move := ""
var _shoot_move := ""
var _shoot_until := 0.0
var _flash: OmniLight3D
var _legs_now := ""
var _stepped_until := 0.0
var _shadow: MeshInstance3D
var _soles := 0.0
var _gunshot: AudioStream
var _audio: AudioStreamPlayer3D
var _cooldown := 0.0
var _dead := false
var _airborne := 0.0

## Where the last bullet went, told before the damage is. False when nobody said.
var _hit_head := false
var _top := 1.0
var _voice: AudioStreamPlayer3D
var _grunt: AudioStreamPlayer3D
var _cries := {}
var _flinch_until := 0.0
var _slowed_until := 0.0
var _death_move := ""

## Footsteps, on the same clock the player uses: 400 milliseconds walking, 300 running. An
## enemy you can hear coming is a different enemy from one that arrives in silence, and this
## is most of what makes a level feel occupied.
var _step_audio: AudioStreamPlayer3D
var _steps: Array[AudioStream] = []
var _next_step := 0.0
var _last_step := -1


func setup(lab: Node3D, target: CharacterBody3D, preset: Dictionary,
		weapon: Dictionary) -> void:
	_lab = lab
	_target = target
	_stances = preset.get("stances", {})
	name = str(preset.get("name", "enemy"))

	for child in get_children():
		if child is Skeleton3D:
			_skeleton = child as Skeleton3D
			_body_anim = child.get_node_or_null("AnimationPlayer")
	var held := holder_of(weapon)
	if _body_anim != null and not held.is_empty():
		if _body_anim.has_animation("ref_aim_" + held):
			_aim_move = "ref_aim_" + held
		if _body_anim.has_animation("ref_shoot_" + held):
			_shoot_move = "ref_shoot_" + held
	print("[npc] %s: arms aim with '%s', fire with '%s'" % [name, _aim_move, _shoot_move])

	_audio = AudioStreamPlayer3D.new()
	_audio.volume_db = -10.0
	_audio.unit_size = 8.0   # audible across a room, not across the level
	add_child(_audio)
	# Tracked, so somebody firing on the other side of a wall is heard through the wall.
	if lab.has_method("track_sound"):
		lab.track_sound(_audio)

	if not weapon.is_empty():
		_hold(weapon)

	# Its own voice, so being hit does not cut off its gunshot.
	_voice = AudioStreamPlayer3D.new()
	_voice.volume_db = CRY_LOUD
	_voice.unit_size = CRY_CARRIES
	add_child(_voice)
	# And a second, so the grunt does not cut off the bullet landing.
	_grunt = AudioStreamPlayer3D.new()
	_grunt.volume_db = CRY_LOUD
	_grunt.unit_size = CRY_CARRIES
	add_child(_grunt)
	if lab.has_method("track_sound"):
		lab.track_sound(_voice)
		lab.track_sound(_grunt)
	_cries = {
		"hit": _load_sounds(HIT_SOUNDS),
		"head": _load_sounds(HEAD_SOUNDS),
		"die": _load_sounds(DEATH_SOUNDS),
		"pain": _load_sounds(PAIN_SOUNDS),
	}

	for child in get_children():
		if child is CollisionShape3D and (child as CollisionShape3D).shape is CapsuleShape3D:
			var capsule := (child as CollisionShape3D).shape as CapsuleShape3D
			_top = (child as CollisionShape3D).position.y + capsule.height * 0.5

	_step_audio = AudioStreamPlayer3D.new()
	_step_audio.volume_db = -11.0
	_step_audio.unit_size = 6.0
	add_child(_step_audio)
	if lab.has_method("track_sound"):
		lab.track_sound(_step_audio)

	_cast_shadow()
	if lab.get("floor_light") != null:
		lab.floor_light.dress(self)

	# Stagger the first shot. Four enemies firing in unison from the instant the level loads
	# killed the first playtest in under three seconds, before anyone had read the HUD.
	_cooldown = randf_range(2.5, 2.5 + FIRE_INTERVAL)


## Which of the body's ways of holding a weapon goes with this one: "ak47", "mp5", "rifle"...
## The weapon says, and one that does not is held like a submachine gun, two hands and level,
## which is wrong for a pistol by less than arms straight out are wrong for anything.
static func holder_of(weapon: Dictionary) -> String:
	if weapon.is_empty():
		return ""
	var held := str(weapon.get("holder_class", ""))
	return held if not held.is_empty() else "mp5"


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

	var model := Figure.hold(_lab, _skeleton, weapon, name)
	if model == null:
		return


## A soft dark patch on the ground under it.
##
## A level lit at night has no sun to throw a shadow, and a figure with nothing under its
## feet looks pasted onto the picture. A blot, not a silhouette.
func _cast_shadow() -> void:
	var soles := 0.0
	for child in get_children():
		if child is CollisionShape3D and (child as CollisionShape3D).shape is CapsuleShape3D:
			soles = (child as CollisionShape3D).position.y \
				- ((child as CollisionShape3D).shape as CapsuleShape3D).height * 0.5

	var size := 64
	var image := Image.create(size, size, false, Image.FORMAT_RGBA8)
	var middle := (size - 1) / 2.0
	for y in size:
		for x in size:
			var away := clampf(Vector2(x - middle, y - middle).length() / middle, 0.0, 1.0)
			image.set_pixel(x, y, Color(0, 0, 0, 0.75 * (1.0 - away) * (1.0 - away)))

	# A flat patch laid on the ground rather than a projected decal: the level is drawn with
	# its light already baked in, and a decal does not show on a surface like that.
	var patch := QuadMesh.new()
	patch.size = Vector2(1.3, 1.3)
	var look := StandardMaterial3D.new()
	look.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	look.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	look.albedo_texture = ImageTexture.create_from_image(image)
	look.cull_mode = BaseMaterial3D.CULL_DISABLED
	patch.material = look
	_shadow = MeshInstance3D.new()
	_shadow.name = "Shadow"
	_shadow.mesh = patch
	_shadow.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	_shadow.top_level = true
	add_child(_shadow)
	_soles = soles


## Keep the patch on whatever is under the feet, lying along it, and fainter the higher the
## body is above it.
func _lay_shadow() -> void:
	if _shadow == null:
		return
	var from := global_position + Vector3(0, _soles + 0.3, 0)
	var query := PhysicsRayQueryParameters3D.create(from, from + Vector3.DOWN * 4.0)
	query.collision_mask = 0xFFFFFFFF & ~(1 << 19)   # not the fattened copy of the level
	query.exclude = [get_rid()]
	var hit := get_world_3d().direct_space_state.intersect_ray(query)
	_shadow.visible = not hit.is_empty()
	if hit.is_empty():
		return
	var normal: Vector3 = hit["normal"]
	var at: Vector3 = hit["position"]
	var side := normal.cross(Vector3.FORWARD)
	if side.length() < 0.01:
		side = normal.cross(Vector3.RIGHT)
	side = side.normalized()
	# A quad faces +Z, so +Z goes along the ground's own normal.
	_shadow.global_transform = Transform3D(Basis(side, normal.cross(side), normal), at + normal * 0.012)
	var drop := (from.y - 0.3) - at.y
	_shadow.transparency = clampf(drop / 2.0, 0.0, 1.0)


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
	var now := Time.get_ticks_msec() / 1000.0
	if distance < SIGHT and distance > KEEP_AWAY:
		var pace := SPEED * (SLOWED if now < _slowed_until else 1.0)
		var step := flat.normalized() * pace
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
	_step_sound()
	_lay_shadow()
	if Stairs.move(self, delta, walking and (is_on_floor() or now < _stepped_until)) > 0.0:
		_stepped_until = now + 0.15


## The same rule as the player's: a step every 400 milliseconds at a walk, and what it sounds
## like comes from the texture underfoot.
func _step_sound() -> void:
	if _dead or not is_on_floor() or _step_audio == null:
		return
	var ground := Vector2(velocity.x, velocity.z).length()
	if ground < 1.0:
		return

	var now := Time.get_ticks_msec() / 1000.0
	if now < _next_step:
		return
	_next_step = now + 0.40

	if _steps.is_empty():
		_load_steps()
	if _steps.is_empty():
		return
	var pick := randi() % _steps.size()
	if _steps.size() > 1 and pick == _last_step:
		pick = (pick + 1) % _steps.size()
	_last_step = pick
	_step_audio.stream = _steps[pick]
	_step_audio.play()


## Concrete, without asking what is underfoot.
##
## The player reads the texture below it and picks a surface from that; an enemy does not,
## because the answer costs a ray and a table lookup per step per body and the difference
## between an enemy walking on tile and on concrete is not what anybody is listening for.
## Named rather than left to be noticed.
func _load_steps() -> void:
	if _lab == null or _lab.vfs == null:
		return
	for named in SoundCatalogue.footsteps("step"):
		var hit: Dictionary = _lab.vfs.find_files("sound/" + str(named),
			PackedStringArray(["wav"]), PackedStringArray(), PackedStringArray(), 1)
		var files: PackedStringArray = hit["files"]
		if files.is_empty():
			continue
		var stream: AudioStream = SoundLoader.load_sound(_lab.vfs, str(files[0]))
		if stream != null:
			_steps.append(stream)


func _stance(walking: bool) -> String:
	if _dead:
		return "die"
	if _airborne > COYOTE:
		return "jump"
	return "walk" if walking else "idle"


func _play(role: String) -> void:
	if _body_anim == null:
		return
	# A flinch runs its course before walking takes the body back.
	if not _dead and Time.get_ticks_msec() / 1000.0 < _flinch_until:
		return
	var wanted := AnimationSets.best(_stances, role)
	if _dead and not _death_move.is_empty():
		wanted = _death_move
	if wanted.is_empty() or not _body_anim.has_animation(wanted):
		return
	# The arms on top of the legs, while it is alive: aiming, or firing for a moment.
	var keep := -1.0
	if not _dead:
		var legs := wanted
		var firing := Time.get_ticks_msec() / 1000.0 < _shoot_until and not _shoot_move.is_empty()
		wanted = Figure.layered(_body_anim, _skeleton, legs, _shoot_move if firing else _aim_move)
		# The legs carry on from where they were when only the arms change.
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


func _fire() -> void:
	if _gunshot != null:
		_audio.stream = _gunshot
		_audio.play()
	_shoot_until = Time.get_ticks_msec() / 1000.0 + 0.2

	if _lab != null and _lab.get("enhanced") == true:
		if _flash == null:
			_flash = _lab.FlashScript.new()
			add_child(_flash)
			# About where the muzzle is: chest high, an arm's length in front.
			_flash.position = Vector3(0, 1.25, -0.7)
		_flash.fire(0.8)

	if _target != null and is_instance_valid(_target) and _target.has_method("take_damage"):
		_target.take_damage(DAMAGE)


## Where the bullet that is about to hurt it went in, and which way it was going. Called
## before take_damage by whatever knows; without it every hit counts as a body hit.
func struck_at(where: Vector3, _along: Vector3) -> void:
	_hit_head = where.y > global_position.y + _top - HEAD_HIGH


func take_damage(amount: int) -> void:
	if _dead:
		return
	var head := _hit_head
	_hit_head = false
	health = maxi(health - amount, 0)
	if health > 0:
		_cry("head" if head else "hit")
		_cry("pain", _grunt)
		_flinch("head_flinch" if head else "gut_flinch")
		_slowed_until = Time.get_ticks_msec() / 1000.0 + SLOWED_FOR
		return

	_dead = true
	# GoldSrc has no ragdolls. Health reaching zero raises deadflag, the hull shrinks to a
	# volume on the floor, bullets pass through, and a death animation freezes on its last
	# frame. A corpse reacts to nothing. Ragdolls here would be a modernisation, not a
	# restoration, and it is worth being clear about which.
	#
	# Passing through is taken literally: off every layer, so a bullet finds the wall behind
	# and the player walks over it, and still on the floor through its mask.
	collision_layer = 0
	_cry("head" if head else "hit")
	_cry("die", _grunt)
	_death_move = _pick_death(head)
	_flinch_until = 0.0
	_play("die")


func _pick_death(head: bool) -> String:
	if _body_anim == null:
		return ""
	if head and _body_anim.has_animation(DEATH_BY_HEAD):
		return DEATH_BY_HEAD
	var have: Array = []
	for named in DEATHS:
		if _body_anim.has_animation(named):
			have.append(named)
	return "" if have.is_empty() else str(have[randi() % have.size()])


func _flinch(named: String) -> void:
	if _body_anim == null or not _body_anim.has_animation(named):
		return
	_body_anim.stop()
	_body_anim.play(named)
	var lasts := minf(_body_anim.get_animation(named).length, FLINCH_AT_MOST)
	_flinch_until = Time.get_ticks_msec() / 1000.0 + lasts


func _cry(kind: String, from: AudioStreamPlayer3D = null) -> void:
	if from == null:
		from = _voice
	var choices: Array = _cries.get(kind, [])
	if choices.is_empty() or from == null:
		return
	from.stream = choices[randi() % choices.size()]
	from.play()


func _load_sounds(names: Array) -> Array:
	var out: Array = []
	if _lab == null or _lab.vfs == null:
		return out
	for named in names:
		var hit: Dictionary = _lab.vfs.find_files("sound/" + str(named).get_basename(),
			PackedStringArray(["wav"]), PackedStringArray(), PackedStringArray(), 1)
		var files: PackedStringArray = hit["files"]
		if files.is_empty():
			continue
		var stream: AudioStream = SoundLoader.load_sound(_lab.vfs, str(files[0]))
		if stream != null:
			out.append(stream)
	return out
