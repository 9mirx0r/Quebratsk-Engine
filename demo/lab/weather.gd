extends Node3D

## The weather a map declares, given back to it.
##
## de_aztec is a storm and says so in its own entity list, which no import had ever read:
##
##     env_rain          one brush volume; rain falls inside it
##     ambient_generic   ambience/rain.wav at four points, looping
##     ambient_generic   ambience/thunder_clap.wav, named "boom", starting silent
##     multi_manager     named "thunder", fires "boom"
##     trigger_multiple  two volumes, wait 45, targeting "thunder"
##     light_environment pitch -70, yaw 245, colour 192 192 192
##
## So the thunder is not a loop and not a random timer. Walking into one of two places sets it
## off, at most once every forty-five seconds, and that is the mapper's decision about where
## the storm should feel closest. The lightning flashes from the same trigger as the sound,
## which is the only way the two can agree.
##
## Nothing here is invented. Every number above comes out of the .bsp.

## How the sky and the sun behave during a strike, in seconds. A real flash is a hard rise and
## a slower fall, sometimes doubled — one bright stroke, a gap, a dimmer one.
const FLASH_RISE := 0.04
const FLASH_FALL := 0.35
const SECOND_STROKE := 0.18

## How much brighter the world gets. The sun's own energy is multiplied, so a map lit dimly
## flashes proportionally rather than being blown out.
const FLASH_GAIN := 6.0

var _sun: DirectionalLight3D
var _sun_energy := 1.0
var _environment: Environment
var _sky_energy := 1.0

var _rain: GPUParticles3D
var _flash := 0.0
var _second_at := -1.0
var _thunder: AudioStreamPlayer3D
var _storm_material: ShaderMaterial

signal struck


## The sun the map declares, as a light rather than as a number in a file.
##
## light_environment carries a pitch, a yaw in its angles, and "_light" as R G B brightness.
## Half-Life's pitch is inverted relative to Godot's, and the brightness is out of roughly 200.
func build_sun(entities: Array) -> DirectionalLight3D:
	for e in entities:
		var entity: Dictionary = e
		if str(entity.get("classname", "")) != "light_environment":
			continue

		_sun = DirectionalLight3D.new()
		_sun.name = "Sun"

		var pitch := float(entity.get("pitch", -45.0))
		var yaw := 0.0
		var angles = entity.get("angles", "")
		if angles is Vector3:
			yaw = (angles as Vector3).y
		elif angles is String:
			var parts := str(angles).split(" ", false)
			if parts.size() >= 2:
				yaw = float(parts[1])
		# GoldSrc points the sun by where it is in the sky; Godot points a light by where it
		# shines. The negated pitch is that difference and nothing more.
		_sun.rotation_degrees = Vector3(pitch, yaw + 180.0, 0.0)

		var lit := str(entity.get("_light", "192 192 192 90")).split(" ", false)
		if lit.size() >= 3:
			_sun.light_color = Color(float(lit[0]) / 255.0, float(lit[1]) / 255.0,
				float(lit[2]) / 255.0)
		if lit.size() >= 4:
			# Out of roughly 200 in the compiler's scale, and kept modest: this is a storm.
			_sun.light_energy = clampf(float(lit[3]) / 200.0, 0.15, 2.0)
		_sun_energy = _sun.light_energy
		_sun.shadow_enabled = true
		add_child(_sun)
		return _sun
	return null


## Rain over the whole level, which is what env_rain declares.
##
## env_rain is a switch and not a volume: its own brush is a sixty-four unit marker cube, and
## treating that as the emission box put a column of water over the player. What it means is
## that this level has weather, so the rain covers the map and follows whoever is looking at
## it — which is what every rain effect since Quake does, because a level three hundred metres
## across would otherwise need a million drops.
##
## Indoor parts of the level get rain they should not. Naming it rather than hiding it: doing
## better means asking the level whether there is sky overhead, which is a raycast per drop.
func build_rain(entities: Array, over: AABB) -> void:
	var declared := false
	for e in entities:
		if str((e as Dictionary).get("classname", "")) == "env_rain":
			declared = true
			break
	if not declared:
		return

	var mesh := QuadMesh.new()
	mesh.size = Vector2(0.02, 0.5)

	var material := StandardMaterial3D.new()
	material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	material.albedo_color = Color(0.75, 0.8, 0.9, 0.35)
	material.billboard_mode = BaseMaterial3D.BILLBOARD_ENABLED
	material.billboard_keep_scale = true
	material.disable_receive_shadows = true
	mesh.material = material

	var process := ParticleProcessMaterial.new()
	process.emission_shape = ParticleProcessMaterial.EMISSION_SHAPE_BOX
	# Wide enough to be weather rather than a shower, and capped so an enormous level does
	# not spread three thousand drops so thin that none of them land near you.
	var reach: float = clampf(maxf(over.size.x, over.size.z) * 0.5, 20.0, 45.0)
	process.emission_box_extents = Vector3(reach, 1.0, reach)
	process.direction = Vector3(0, -1, 0)
	process.spread = 2.0
	process.initial_velocity_min = 14.0
	process.initial_velocity_max = 18.0
	process.gravity = Vector3(0, -9.8, 0)
	process.scale_min = 0.6
	process.scale_max = 1.4

	_rain = GPUParticles3D.new()
	_rain.name = "Rain"
	_rain.draw_pass_1 = mesh
	_rain.process_material = process
	_rain.amount = 3000
	_rain.lifetime = 2.2
	_rain.preprocess = 2.2
	# Local to the player, so a level three hundred metres across does not need a million
	# drops to look wet. It follows you, which is what every rain effect since Quake does.
	_rain.local_coords = false
	_rain.visibility_aabb = AABB(Vector3(-reach, -40.0, -reach),
		Vector3(reach * 2.0, 60.0, reach * 2.0))
	add_child(_rain)


## Keep the rain over whoever is looking at it.
func follow(who: Node3D) -> void:
	if _rain != null and who != null:
		# Snapped to a grid rather than glued to the player. A world-space emitter that moves
		# every frame lays its drops along the path it travelled, which is the diagonal streak
		# of water that appeared when this followed exactly.
		var at := who.global_position + Vector3(0, 22.0, 0)
		_rain.global_position = Vector3(snappedf(at.x, 8.0), at.y, snappedf(at.z, 8.0))


## The sky whose clouds move, so a strike can light them from inside.
func set_storm_sky(sky: Sky) -> void:
	if sky != null and sky.sky_material is ShaderMaterial:
		_storm_material = sky.sky_material as ShaderMaterial


func set_environment(environment: Environment) -> void:
	_environment = environment
	if environment != null:
		_sky_energy = environment.background_energy_multiplier


## The thunder the map named, ready to be set off.
func arm_thunder(player: AudioStreamPlayer3D) -> void:
	_thunder = player


## A strike: the sound the map declares and the light that belongs with it, together.
##
## Together is the whole point. A flash without its thunder is a glitch and thunder without
## its flash is a sound effect; the map ties them to one trigger and so does this.
func strike() -> void:
	_flash = 1.0
	_second_at = SECOND_STROKE
	if _thunder != null and not _thunder.playing:
		_thunder.play()
	struck.emit()


func _process(delta: float) -> void:
	if _flash <= 0.0 and _second_at < 0.0:
		return

	if _second_at >= 0.0:
		_second_at -= delta
		if _second_at < 0.0:
			# The second, dimmer stroke. A single clean flash reads as a light switch.
			_flash = maxf(_flash, 0.55)

	_flash = maxf(_flash - delta / FLASH_FALL, 0.0)
	# Squared, so the fall is fast at first and lingers, which is what a discharge does.
	var lit := _flash * _flash

	if _sun != null:
		_sun.light_energy = _sun_energy * (1.0 + lit * FLASH_GAIN)
	if _storm_material != null:
		# The sky itself, not its exposure. With the lit panorama loaded this cross-fades to
		# a photograph of the same storm discharging, which is a different thing from turning
		# the brightness up on the first one.
		_storm_material.set_shader_parameter("flash", lit)
	elif _environment != null:
		_environment.background_energy_multiplier = _sky_energy * (1.0 + lit * 2.0)
