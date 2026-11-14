extends Node3D

## A Source particle effect (a system out of a .pcf, with the systems it brings along),
## built out of Godot particles and fired on demand.
##
## The file is read by the engine (load_particles); what each step means is from the Valve
## Developer Wiki's pages on the particle editor's operators. Only the steps a short burst
## such as a muzzle flash is made of are followed: how many come out and when, how long they
## live, how big, what colour, which way they go, and how size, colour and alpha change over
## a life. Anything else in the file is left out and named once in the log, so a system that
## looks wrong says why.
##
## Source keeps sizes and speeds in inches and its axes Z up, X forward; the effect is built
## in metres in the axes of whatever it hangs from, forward being -z.

const UNIT := 0.0254
## Samples taken of each change over a life, for the curves and the colour ramp.
const STEPS := 16

## Steps that are followed, or have nothing to draw. Anything else is reported.
const FOLLOWED := ["emit_instantaneously", "emit_continuously", "Lifetime Random",
	"Radius Random", "Color Random", "Alpha Random", "Rotation Random",
	"Position Within Sphere Random", "Movement Basic", "Radius Scale", "Color Fade",
	"Alpha Fade Out Random", "Alpha Fade In Random", "Alpha Fade and Decay",
	"Lifespan Decay", "Movement Lock to Control Point", "render_animated_sprites",
	"Rotation Basic", "Rotation Speed Random", "remap initial scalar",
	"remap scalar to vector"]

## The fields a remap step can read or write, as the particle editor numbers them.
const FIELD_POSITION := 0
const FIELD_LIFETIME := 1
const FIELD_RADIUS := 3
const FIELD_CREATION_TIME := 8

## Every system in every particle file read so far, by file.
static var _files := {}
## Steps already reported as not followed, so each is named once.
static var _reported := {}

var _parts: Array[GPUParticles3D] = []
var _delays: Array[float] = []


## The file that holds the system called `system_name`, among the particle files mounted, or
## an empty string.
static func find_file(lab: Node, system_name: String) -> String:
	var hit: Dictionary = lab.vfs.find_files("particles/", PackedStringArray(["pcf"]),
		PackedStringArray(), PackedStringArray(), 400)
	for uri in hit.get("files", []):
		for system in _systems(lab, str(uri)):
			if str((system as Dictionary)["name"]).to_lower() == system_name.to_lower():
				return str(uri)
	return ""


static func _systems(lab: Node, uri: String) -> Array:
	if not _files.has(uri):
		_files[uri] = lab.importer.load_particles(uri)
	return _files[uri]


## The effect called `system_name` in the particle file `uri`, ready to fire, or null when the
## file has no such system.
static func build(lab: Node, uri: String, system_name: String) -> Node3D:
	var by_name := {}
	for system in _systems(lab, uri):
		by_name[str((system as Dictionary)["name"]).to_lower()] = system
	if not by_name.has(system_name.to_lower()):
		return null
	var effect = new()
	effect.name = system_name
	effect._add(lab, by_name, by_name[system_name.to_lower()], 0.0, 0)
	return effect


## Start it again from the beginning, each system after its own delay.
func fire() -> void:
	for i in _parts.size():
		var part := _parts[i]
		if _delays[i] <= 0.0:
			part.restart()
		else:
			get_tree().create_timer(_delays[i]).timeout.connect(part.restart)


func _add(lab: Node, by_name: Dictionary, system: Dictionary, delay: float, depth: int) -> void:
	var part := _particles(lab, system)
	if part != null:
		add_child(part)
		_parts.append(part)
		_delays.append(delay)
	# A child may bring its own; a loop of them is stopped by depth.
	if depth > 8:
		return
	for child in system.get("children", []):
		var named := str((child as Dictionary)["name"]).to_lower()
		if by_name.has(named):
			_add(lab, by_name, by_name[named], delay + float(child["delay"]), depth + 1)


## Source's axes to Godot's: X forward, Y left, Z up to -z, -x, +y.
static func _axes(v: Vector3) -> Vector3:
	return Vector3(-v.y, v.z, -v.x)


## The steps of `system` of one kind, by function, the first of each.
static func _first(system: Dictionary, kind: String, function: String) -> Dictionary:
	for step in system.get(kind, []):
		if str(step["function"]) == function:
			return step["properties"]
	return {}


static func _all(system: Dictionary, kind: String, function: String) -> Array:
	var out: Array = []
	for step in system.get(kind, []):
		if str(step["function"]) == function:
			out.append(step["properties"])
	return out


## Source's bias curve: 0.5 is a straight line, less leans late, more leans early.
static func _bias(t: float, bias: float) -> float:
	bias = clampf(bias, 0.001, 0.999)
	return t / ((1.0 / bias - 2.0) * (1.0 - t) + 1.0)


static func _particles(lab: Node, system: Dictionary) -> GPUParticles3D:
	var called := str(system["name"])
	var own: Dictionary = system.get("properties", {})
	if system.get("renderers", []).is_empty():
		return null   # nothing to draw: it only carries its children
	for kind in ["emitters", "initializers", "operators", "renderers"]:
		for step in system.get(kind, []):
			var function := str(step["function"])
			if function not in FOLLOWED and not _reported.has(function):
				_reported[function] = true
				print("[particles] '%s' (in %s) is not followed" % [function, called])

	var part := GPUParticles3D.new()
	part.name = called
	part.one_shot = true
	part.emitting = false
	part.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	part.local_coords = not _first(system, "operators", "Movement Lock to Control Point").is_empty()
	var look := ParticleProcessMaterial.new()
	look.gravity = Vector3.ZERO

	# How long each lives and how big, walked in the order the initializers run, since a later
	# one overwrites or scales what an earlier one set. Source's defaults are a second and the
	# system's own radius (in inches; a sprite is twice that wide).
	var life := Vector2(1.0, 1.0)
	var radius := Vector2.ONE * float(own.get("radius", 5.0))
	var stretch := {}
	for step in system.get("initializers", []):
		var does: Dictionary = step["properties"]
		match str(step["function"]):
			"Lifetime Random":
				life = Vector2(float(does.get("lifetime_min", 1.0)), float(does.get("lifetime_max", 1.0)))
			"Radius Random":
				radius = Vector2(float(does.get("radius_min", 5.0)), float(does.get("radius_max", 5.0)))
			"remap initial scalar":
				var field := int(does.get("output field", -1))
				var low := float(does.get("output minimum", 0.0))
				var high := float(does.get("output maximum", 1.0))
				var scaled := int(does.get("output is scalar of initial random range", 0)) != 0
				var span := Vector2(minf(low, high), maxf(low, high))
				if field == FIELD_LIFETIME:
					life = Vector2(life.x * span.x, life.y * span.y) if scaled else span
				elif field == FIELD_RADIUS:
					radius = Vector2(radius.x * span.x, radius.y * span.y) if scaled else span
			"remap scalar to vector":
				# Placed by when it was made: a burst spread over a few hundredths of a second
				# comes out strung along a line, which is how a flash gets its length.
				if int(does.get("input field", -1)) == FIELD_CREATION_TIME and int(does.get("output field", -1)) == FIELD_POSITION:
					stretch = does
	var longest := maxf(maxf(life.x, life.y), 0.01)
	var shortest := minf(life.x, life.y)
	part.lifetime = longest
	part.randomness = 0.0
	look.lifetime_randomness = clampf(1.0 - shortest / longest, 0.0, 1.0)

	# How many, and when.
	# Every emitter adds its own; one that streams for less than a twentieth of a second is a
	# burst to the eye, and is drawn as one.
	var most := int(own.get("max_particles", 1000))
	var count := 0
	var streamed := 0.0
	for burst in _all(system, "emitters", "emit_instantaneously"):
		count += int(burst.get("num_to_emit", 1))
	for stream in _all(system, "emitters", "emit_continuously"):
		var lasts := float(stream.get("emission_duration", 0.0))
		if lasts <= 0.0:
			lasts = longest   # forever, which a burst on demand cannot be
		count += int(ceil(float(stream.get("emission_rate", 100.0)) * lasts))
		streamed = maxf(streamed, lasts)
	part.amount = clampi(count, 1, maxi(most, 1))
	part.explosiveness = 1.0 if streamed <= 0.05 else 0.0
	part.preprocess = 0.0
	part.fixed_fps = 0
	part.visibility_aabb = AABB(Vector3(-2, -2, -2), Vector3(4, 4, 4))

	# Where and which way. Out of a sphere, at a speed outward and one of its own.
	var sphere := _first(system, "initializers", "Position Within Sphere Random")
	var outward_max := float(sphere.get("speed_max", 0.0))
	var outward_min := float(sphere.get("speed_min", outward_max))
	var along_max := _axes(sphere.get("speed_in_local_coordinate_system_max", Vector3.ZERO))
	var along_min := _axes(sphere.get("speed_in_local_coordinate_system_min", Vector3.ZERO))
	var reach := float(sphere.get("distance_max", 0.0))
	if reach > 0.0:
		look.emission_shape = ParticleProcessMaterial.EMISSION_SHAPE_SPHERE
		look.emission_sphere_radius = reach * UNIT
	if not stretch.is_empty():
		var from := _axes(stretch.get("output minimum", Vector3.ZERO)) * UNIT
		var to := _axes(stretch.get("output maximum", Vector3.ZERO)) * UNIT
		look.emission_shape = ParticleProcessMaterial.EMISSION_SHAPE_BOX
		look.emission_box_extents = ((to - from) / 2.0).abs()
		look.emission_shape_offset = (from + to) / 2.0
	if along_max.length() > 0.0 or along_min.length() > 0.0:
		look.direction = (along_min + along_max).normalized()
		look.spread = 10.0
		look.initial_velocity_min = (along_min.length() + outward_min) * UNIT
		look.initial_velocity_max = (along_max.length() + outward_max) * UNIT
	else:
		look.spread = 180.0
		look.initial_velocity_min = outward_min * UNIT
		look.initial_velocity_max = outward_max * UNIT

	var moving := _first(system, "operators", "Movement Basic")
	if not moving.is_empty():
		look.gravity = _axes(moving.get("gravity", Vector3.ZERO)) * UNIT
		# Source takes a share of the speed away each tick of a thirtieth of a second.
		var drag := float(moving.get("drag", 0.0))
		look.damping_min = drag * 30.0 * look.initial_velocity_max
		look.damping_max = look.damping_min

	# How big.
	var big := maxf(maxf(radius.x, radius.y), 0.01)
	look.scale_min = minf(radius.x, radius.y) / big
	look.scale_max = 1.0
	var scale_curve := _radius_over_life(_all(system, "operators", "Radius Scale"))
	if scale_curve != null:
		var holder := CurveTexture.new()
		holder.curve = scale_curve
		look.scale_curve = holder

	# Turned on the screen.
	var turned := _first(system, "initializers", "Rotation Random")
	if not turned.is_empty():
		var at := float(turned.get("rotation_initial", 0.0))
		look.angle_min = at + float(turned.get("rotation_offset_min", 0.0))
		look.angle_max = at + float(turned.get("rotation_offset_max", 360.0))
	var spun := _first(system, "initializers", "Rotation Speed Random")
	if not spun.is_empty():
		var constant := float(spun.get("rotation_speed_constant", 0.0))
		look.angular_velocity_min = constant + float(spun.get("rotation_speed_random_min", 0.0))
		look.angular_velocity_max = constant + float(spun.get("rotation_speed_random_max", 0.0))

	# What colour, at birth and over a life.
	var base: Color = own.get("color", Color.WHITE)
	var picked := _first(system, "initializers", "Color Random")
	var alphas := _first(system, "initializers", "Alpha Random")
	var first_colour := base
	var second_colour := base
	if not picked.is_empty():
		first_colour = picked.get("color1", base)
		second_colour = picked.get("color2", base)
	if not alphas.is_empty():
		first_colour.a = float(alphas.get("alpha_min", 255)) / 255.0
		second_colour.a = float(alphas.get("alpha_max", 255)) / 255.0
	var at_birth := Gradient.new()
	at_birth.set_color(0, first_colour)
	at_birth.set_color(1, second_colour)
	var birth := GradientTexture1D.new()
	birth.gradient = at_birth
	look.color_initial_ramp = birth
	var over_life := _colour_over_life(system, longest)
	if over_life != null:
		var ramp := GradientTexture1D.new()
		ramp.gradient = over_life
		look.color_ramp = ramp

	part.process_material = look

	# Drawn as the material says, facing the camera.
	var quad := QuadMesh.new()
	quad.size = Vector2.ONE * big * 2.0 * UNIT
	var surface := _material(lab, str(own.get("material", "")))
	quad.material = surface
	part.draw_pass_1 = quad
	return part


## A system's material, as a sprite on a card that faces the camera, coloured by its particle.
static func _material(lab: Node, path: String) -> Material:
	var uri := ""
	if not path.is_empty():
		var wanted := "materials/" + path.replace("\\", "/").to_lower()
		if not wanted.ends_with(".vmt"):
			wanted += ".vmt"
		var hit: Dictionary = lab.vfs.find_files(wanted.get_file().get_basename(),
			PackedStringArray(["vmt"]), PackedStringArray(), PackedStringArray(), 32)
		for file in hit.get("files", []):
			if str(file).to_lower().ends_with(wanted):
				uri = str(file)
				break
	var surface: StandardMaterial3D = null
	if not uri.is_empty():
		surface = lab.importer.load_material(uri)
	if surface == null:
		print("[particles] material '%s' not found: a plain card" % path)
		surface = StandardMaterial3D.new()
		surface.blend_mode = BaseMaterial3D.BLEND_MODE_ADD
	surface = surface.duplicate()
	surface.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	surface.billboard_mode = BaseMaterial3D.BILLBOARD_PARTICLES
	surface.billboard_keep_scale = true
	surface.vertex_color_use_as_albedo = true
	if surface.blend_mode != BaseMaterial3D.BLEND_MODE_ADD:
		surface.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	surface.disable_receive_shadows = true
	return surface


## The size over a life, from every Radius Scale step: within its window each sets the size,
## after it the size stays where it left it, and the latest to have acted wins.
static func _radius_over_life(steps: Array) -> Curve:
	if steps.is_empty():
		return null
	var curve := Curve.new()
	var values: Array[float] = []
	var top := 1.0
	for i in STEPS + 1:
		var t := float(i) / STEPS
		var value := 1.0
		var latest := -1.0
		for step in steps:
			var start := float(step.get("start_time", 0.0))
			var end := maxf(float(step.get("end_time", 1.0)), start + 0.0001)
			if t < start:
				continue
			var acted := minf(t, end)
			if acted < latest:
				continue
			latest = acted
			var along := _bias(clampf((t - start) / (end - start), 0.0, 1.0),
				float(step.get("scale_bias", 0.5)))
			value = lerpf(float(step.get("radius_start_scale", 1.0)),
				float(step.get("radius_end_scale", 1.0)), along)
		values.append(value)
		top = maxf(top, value)
	curve.max_value = top
	for i in values.size():
		curve.add_point(Vector2(float(i) / STEPS, values[i]), 0.0, 0.0,
			Curve.TANGENT_LINEAR, Curve.TANGENT_LINEAR)
	return curve


## Colour and alpha over a life, as a ramp the colour at birth is multiplied by.
static func _colour_over_life(system: Dictionary, life: float) -> Gradient:
	var fade := _first(system, "operators", "Color Fade")
	var fade_out := _first(system, "operators", "Alpha Fade Out Random")
	var fade_in := _first(system, "operators", "Alpha Fade In Random")
	var decay := _first(system, "operators", "Alpha Fade and Decay")
	if fade.is_empty() and fade_out.is_empty() and fade_in.is_empty() and decay.is_empty():
		return null
	var ramp := Gradient.new()
	var offsets := PackedFloat32Array()
	var colours := PackedColorArray()
	for i in STEPS + 1:
		var t := float(i) / STEPS
		var colour := Color.WHITE
		if not fade.is_empty():
			var towards: Color = fade.get("color_fade", Color.WHITE)
			var start := float(fade.get("fade_start_time", 0.0))
			var end := maxf(float(fade.get("fade_end_time", 1.0)), start + 0.0001)
			var along := clampf((t - start) / (end - start), 0.0, 1.0)
			if int(fade.get("ease_in_and_out", 0)) != 0:
				along = smoothstep(0.0, 1.0, along)
			colour = Color.WHITE.lerp(Color(towards, 1.0), along)
		var alpha := 1.0
		if not fade_in.is_empty():
			var lasts := float(fade_in.get("fade in time max", 0.25))
			if int(fade_in.get("proportional 0/1", 1)) == 0:
				lasts /= life
			alpha *= clampf(t / maxf(lasts, 0.0001), 0.0, 1.0)
		if not fade_out.is_empty():
			var lasts := float(fade_out.get("fade out time max", 0.25))
			if int(fade_out.get("proportional 0/1", 1)) == 0:
				lasts /= life
			alpha *= clampf((1.0 - t) / maxf(lasts, 0.0001), 0.0, 1.0)
		if not decay.is_empty():
			var from := float(decay.get("start_alpha", 1.0))
			var to := float(decay.get("end_alpha", 0.0))
			var in_end := float(decay.get("end_fade_in_time", 0.0))
			var out_start := float(decay.get("start_fade_out_time", 0.5))
			var out_end := maxf(float(decay.get("end_fade_out_time", 1.0)), out_start + 0.0001)
			var a := from
			if in_end > 0.0 and t < in_end:
				a = lerpf(0.0, from, t / in_end)
			elif t >= out_start:
				a = lerpf(from, to, clampf((t - out_start) / (out_end - out_start), 0.0, 1.0))
			alpha *= a
		colour.a = alpha
		offsets.append(t)
		colours.append(colour)
	ramp.offsets = offsets
	ramp.colors = colours
	return ramp
