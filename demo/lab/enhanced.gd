extends Node

## The level as it could look now, laid over the level as it was.
##
## None of this is in the game. A GoldSrc wall is one small texture multiplied by light worked
## out once when the level was compiled, and that is all the classic material does. This puts
## a second material over each of those surfaces which keeps that light exactly and adds what
## a 1998 renderer had no way of doing:
##
##   detail     a second, much finer texture multiplied in close up, which is how the game's
##              own later versions sharpened a wall (maps/<map>_detail.txt and gfx/detail/).
##              A level with no such file gets one picked by what each texture is made of.
##   relief     the fine texture read as height, so a lamp or a gunshot rakes across it.
##   shine      metal, tile and glass reflect; concrete and dirt do not. With the screen-space
##              reflections switched on in the environment that is a floor showing the room.
##   shadows    whoever stands in the sun casts one on the level. The level's own shadows
##              stay the painted ones, so nothing is shadowed twice.
##
## The classic materials stay in the mesh underneath, so switching back is exact.

const SurfaceMaterial := preload("res://addons/quebratsk_editor/surface_material.gd")
const SoundCatalogue := preload("res://addons/quebratsk_editor/sound_catalogue.gd")

## The render layer that means "this is standing in the sun", which is all the sun's shadow
## is drawn from.
const SUNLIT_LAYER := 4
## How bright the floor has to be painted for whoever stands on it to be in the sun: no
## shadow at all below the first, a whole one above the second, and part of one between.
const SUN_ON := 0.66
const SUN_OFF := 0.46
## How fast a shadow comes and goes, in whole shadows per second.
const SUN_EASE := 1.6

## How rough and how reflective each of the game's own materials is, by the letter the game
## gives it in sound/materials.txt. Anything unlisted is concrete, as it is to the game.
const FINISH := {
	"c": Vector2(0.92, 0.15),
	"m": Vector2(0.38, 0.65),
	"v": Vector2(0.45, 0.55),
	"g": Vector2(0.50, 0.50),
	"t": Vector2(0.22, 0.55),
	"y": Vector2(0.08, 0.60),
	"p": Vector2(0.30, 0.50),
	"s": Vector2(0.25, 0.50),
	"w": Vector2(0.70, 0.25),
	"d": Vector2(1.00, 0.00),
}

## The detail texture to try for a material when the level names none, first found wins.
const DETAIL_FOR := {
	"c": ["detail/concretel", "detail/concretegroundd", "detail/stone4"],
	"m": ["detail/metal01", "detail/dust_metal2", "detail/rust"],
	"v": ["detail/metal01", "detail/dust_metal2"],
	"g": ["detail/metal01"],
	"t": ["detail/cfloor2M", "detail/concretel"],
	"w": ["detail/wood4", "detail/p4wood", "detail/wood9"],
	"d": ["detail/SoilSand0", "detail/detailrocks"],
}

const SHADER := """
shader_type spatial;
render_mode ambient_light_disabled;
uniform sampler2D albedo_texture : filter_linear_mipmap_anisotropic, repeat_enable, hint_default_white;
uniform sampler2D lightmap_texture : filter_linear, repeat_disable, hint_default_white;
uniform sampler2D detail_texture : filter_linear_mipmap_anisotropic, repeat_enable, hint_default_white;
uniform vec2 detail_scale = vec2(1.0);
uniform float detail_mean = 1.0;
uniform float detail_amount = 0.0;
uniform float brightness : hint_range(0.0, 1.0) = 1.0;
uniform float roughness = 0.92;
uniform float shine = 0.15;
uniform float relief = 1.0;
uniform float wet = 0.0;
uniform float shadow_depth = 0.0;
uniform vec3 sun_direction = vec3(0.0, 1.0, 0.0);
uniform float smooth_light = 0.0;
varying vec3 sun_part;
varying float gloss;

vec3 to_linear(vec3 c) {
	return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));
}
vec3 game_light(vec3 stored) {
	vec3 f = pow(min(stored * 1.3195, vec3(1.0)), vec3(2.5));
	float knee = 0.125 - brightness * brightness * 0.075;
	f = mix(f / knee * 0.125, 0.125 + (f - knee) / (1.0 - knee) * 0.875, step(vec3(knee), f));
	return pow(f, vec3(0.4));
}
// The painted light through a cubic filter in four taps instead of the two-by-two the card
// does by itself: the same picture, without the diamond at every texel.
vec3 smooth_lightmap(vec2 uv) {
	vec2 size = vec2(textureSize(lightmap_texture, 0));
	vec2 at = uv * size - 0.5;
	vec2 f = fract(at);
	at -= f;
	vec2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
	vec2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
	vec2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
	vec2 w3 = f * f * (-0.5 + 0.5 * f);
	vec2 s0 = w0 + w1;
	vec2 s1 = w2 + w3;
	vec2 t0 = (at - 0.5 + w1 / s0 + 0.5) / size;
	vec2 t1 = (at + 1.5 + w3 / s1 + 0.5) / size;
	return max((texture(lightmap_texture, vec2(t0.x, t0.y)).rgb * s0.x
		+ texture(lightmap_texture, vec2(t1.x, t0.y)).rgb * s1.x) * s0.y
		+ (texture(lightmap_texture, vec2(t0.x, t1.y)).rgb * s0.x
		+ texture(lightmap_texture, vec2(t1.x, t1.y)).rgb * s1.x) * s1.y, vec3(0.0));
}
void fragment() {
	vec4 tex = texture(albedo_texture, UV);
	vec3 stored = mix(texture(lightmap_texture, UV2).rgb, smooth_lightmap(UV2), smooth_light);
	vec3 light = game_light(stored);
	vec3 base = pow(tex.rgb, vec3(0.8));

	// Close up only. Far away the fine texture is below a pixel and averages to nothing.
	float near = 1.0 - smoothstep(6.0, 22.0, -VERTEX.z);
	float height = dot(tex.rgb, vec3(0.299, 0.587, 0.114));
	if (detail_amount > 0.0) {
		vec3 fine = texture(detail_texture, UV * detail_scale).rgb / detail_mean;
		base *= mix(vec3(1.0), fine, detail_amount * near);
		height = mix(height, dot(fine, vec3(0.333)) * 0.5, 0.75);
	}

	// The texture read as a height: brighter stands proud. Enough to catch a lamp.
	vec3 n = normalize(NORMAL);
	vec3 dpx = dFdx(VERTEX);
	vec3 dpy = dFdy(VERTEX);
	vec3 r1 = cross(dpy, n);
	vec3 r2 = cross(n, dpx);
	float det = dot(dpx, r1);
	vec3 grad = sign(det) * (dFdx(height) * r1 + dFdy(height) * r2);
	NORMAL = normalize(abs(det) * n - 0.012 * relief * near * grad);

	// Rain lies on what faces up.
	vec3 up = (VIEW_MATRIX * vec4(0.0, 1.0, 0.0, 0.0)).xyz;
	float pooled = wet * smoothstep(0.6, 0.95, dot(n, up));
	base *= mix(1.0, 0.8, pooled);

	// The sun is already in the painted light. Part of that light is handed to the sun itself
	// where a surface faces it, so that the sun can take it away again behind somebody
	// standing in it. With nobody there the two parts add up to what was painted.
	vec3 sunward = normalize((VIEW_MATRIX * vec4(sun_direction, 0.0)).xyz);
	float handed = shadow_depth * smoothstep(0.0, 0.15, dot(n, sunward));
	sun_part = to_linear(light) * handed;
	gloss = mix(shine, 0.7, pooled);
	EMISSION = to_linear(clamp(base * light, 0.0, 1.0)) * (1.0 - handed);
	ALBEDO = to_linear(clamp(base, 0.0, 1.0));
	ROUGHNESS = mix(roughness, 0.12, pooled);
	SPECULAR = gloss;
%s}
void light() {
	if (LIGHT_IS_DIRECTIONAL) {
		DIFFUSE_LIGHT += ATTENUATION * sun_part;
	} else {
		float towards = clamp(dot(NORMAL, LIGHT), 0.0, 1.0);
		DIFFUSE_LIGHT += towards * ATTENUATION * LIGHT_COLOR / PI;
		float power = 2.0 / pow(max(ROUGHNESS, 0.08), 4.0) - 2.0;
		float glint = pow(max(dot(NORMAL, normalize(VIEW + LIGHT)), 0.0), power) * (power + 8.0) / (8.0 * PI);
		SPECULAR_LIGHT += min(glint, 8.0) * towards * ATTENUATION * LIGHT_COLOR * gloss / PI;
	}
}
"""

var _lab: Node
var _map: Node3D
var _sun: DirectionalLight3D
var _environment: Environment
var _on := false
var _plain: Shader
var _masked: Shader
var _made := {}
var _covered: Array = []          # [MeshInstance3D, surface index]
var _named := {}                  # texture name (lower) -> { "texture", "scale", "mean" }
var _detail := {}                 # path -> { "texture", "mean" } or null
var _next := 0
var _wet := 0.0
var _was := {}                    # what the environment and the sun were before


## `map` is the built level, `sun` its light_environment (null when it has none) and `rain`
## whether the level declares weather.
func setup(lab: Node, map: Node3D, sun: DirectionalLight3D, environment: Environment,
		rain: bool) -> void:
	_lab = lab
	_map = map
	_sun = sun
	_environment = environment
	_wet = 1.0 if rain else 0.0
	_read_detail_list()
	if _environment != null:
		_was["ssr"] = _environment.ssr_enabled
		_was["adjust"] = _environment.adjustment_enabled
	if _sun != null:
		_was["casters"] = _sun.shadow_caster_mask


## Switch the improved look on or off. Returns how many surfaces changed.
func set_on(on: bool) -> int:
	if on == _on or _map == null:
		return 0
	_on = on
	var changed := _cover() if on else _uncover()

	if _environment != null:
		_environment.ssr_enabled = on or bool(_was.get("ssr", false))
		_environment.ssr_max_steps = 96
		_environment.ssr_fade_in = 0.1
		_environment.ssr_fade_out = 3.0
		_environment.ssr_depth_tolerance = 0.3
		# A little more contrast and colour than a 1999 palette gives by itself.
		_environment.adjustment_enabled = on or bool(_was.get("adjust", false))
		_environment.adjustment_contrast = 1.06 if on else 1.0
		_environment.adjustment_saturation = 1.08 if on else 1.0
	if _sun != null:
		# Only what is marked as standing in the sun is drawn into its shadow. The level's
		# own shadows are the painted ones.
		_sun.shadow_caster_mask = SUNLIT_LAYER if on else int(_was.get("casters", 0xFFFFF))
		_sun.directional_shadow_max_distance = 70.0
		_sun.shadow_blur = 1.0
		_sun.shadow_bias = 0.05
	if not on:
		_mark_all(false)
	return changed


func is_on() -> bool:
	return _on


# -------------------------------------------------------------------- surfaces ----

func _cover() -> int:
	var changed := 0
	var with_detail := 0
	var stack: Array = [_map]
	while not stack.is_empty():
		var node: Node = stack.pop_back()
		for child in node.get_children():
			stack.append(child)
		var instance := node as MeshInstance3D
		if instance == null or instance.mesh == null:
			continue
		for i in instance.mesh.get_surface_count():
			if instance.get_surface_override_material(i) != null:
				continue
			var painted := instance.mesh.surface_get_material(i) as ShaderMaterial
			if painted == null or SurfaceMaterial.lightmap_of(painted) == null:
				continue
			var better := _material_for(painted)
			instance.set_surface_override_material(i, better)
			_covered.append([instance, i])
			changed += 1
			if better.get_shader_parameter("detail_texture") != null:
				with_detail += 1
	print("[enhanced] %d surface(s) improved, %d with a detail texture (%d named by the level)"
		% [changed, with_detail, _named.size()])
	return changed


func _uncover() -> int:
	var restored := 0
	for pair in _covered:
		var instance = pair[0]
		if is_instance_valid(instance):
			(instance as MeshInstance3D).set_surface_override_material(int(pair[1]), null)
			restored += 1
	_covered.clear()
	return restored


func _material_for(painted: ShaderMaterial) -> ShaderMaterial:
	var key := painted.get_instance_id()
	if _made.has(key):
		return _made[key]

	var cut := painted.shader != null and painted.shader.code.contains("ALPHA_SCISSOR")
	var material := ShaderMaterial.new()
	material.resource_name = painted.resource_name
	material.shader = _shader(cut)
	var texture := SurfaceMaterial.texture_of(painted)
	material.set_shader_parameter("albedo_texture", texture)
	material.set_shader_parameter("lightmap_texture", SurfaceMaterial.lightmap_of(painted))
	var bright = painted.get_shader_parameter("brightness")
	if bright != null:
		material.set_shader_parameter("brightness", bright)
	material.set_shader_parameter("wet", _wet)
	if _sun != null:
		material.set_shader_parameter("shadow_depth", 0.8)
		material.set_shader_parameter("sun_direction", _sun.global_basis.z)

	var named := painted.resource_name.to_lower()
	var letter := _letter_of(named)
	var finish: Vector2 = FINISH.get(letter, FINISH["c"])
	material.set_shader_parameter("roughness", finish.x)
	material.set_shader_parameter("shine", finish.y)

	var detail: Dictionary = _detail_for(named, letter, texture)
	if not detail.is_empty():
		material.set_shader_parameter("detail_texture", detail["texture"])
		material.set_shader_parameter("detail_scale", detail["scale"])
		material.set_shader_parameter("detail_mean", detail["mean"])
		material.set_shader_parameter("detail_amount", detail["amount"])

	_made[key] = material
	return material


func _shader(cut: bool) -> Shader:
	if cut:
		if _masked == null:
			_masked = Shader.new()
			_masked.code = SHADER % "\tALPHA = tex.a;\n\tALPHA_SCISSOR_THRESHOLD = 0.5;\n"
		return _masked
	if _plain == null:
		_plain = Shader.new()
		_plain.code = SHADER % ""
	return _plain


## The letter the game's own material list gives a texture, "c" for anything it does not list.
func _letter_of(named: String) -> String:
	var table: Dictionary = SoundCatalogue._materials
	return str(table.get(named.substr(0, SoundCatalogue.NAME_LENGTH), "c"))


# ---------------------------------------------------------------------- detail ----

## maps/<map>_detail.txt: a texture's name, the picture to lay over it (under gfx/, without
## its extension) and how many times that picture repeats across the texture each way.
func _read_detail_list() -> void:
	_named.clear()
	if _lab == null:
		return
	var map_name := str(_lab.get("map_name")).to_lower()
	var wanted := "maps/%s_detail.txt" % map_name
	var hit: Dictionary = _lab.vfs.find_files(wanted, PackedStringArray(["txt"]),
		PackedStringArray(), PackedStringArray(), 4)
	for uri in hit.get("files", PackedStringArray()):
		if not str(uri).to_lower().ends_with(wanted):
			continue
		var bytes: PackedByteArray = _lab.vfs.read_file(str(uri))
		for raw in bytes.get_string_from_ascii().split("\n", false):
			var line := raw.strip_edges().replace("\t", " ")
			if line.is_empty() or line.begins_with("//"):
				continue
			var parts := line.split(" ", false)
			if parts.size() < 2:
				continue
			var across := Vector2(1.0, 1.0)
			if parts.size() >= 4:
				across = Vector2(float(parts[2]), float(parts[3]))
			_named[str(parts[0]).to_lower()] = {"path": str(parts[1]), "scale": across}
		break


func _detail_for(named: String, letter: String, texture: Texture2D) -> Dictionary:
	# What the level itself asks for, exactly as it asks for it.
	if _named.has(named):
		var asked: Dictionary = _named[named]
		var loaded = _load_detail(str(asked["path"]))
		if loaded != null:
			return {"texture": loaded["texture"], "mean": loaded["mean"],
				"scale": asked["scale"], "amount": 1.0}

	# Otherwise by what it is made of, and never on what is not a wall: a texture with holes
	# in it, water, the sky, a screen or a pane of glass.
	if named.begins_with("{") or named.begins_with("!") or named.begins_with("sky") \
			or letter == "y" or letter == "p" or texture == null:
		return {}
	for path in DETAIL_FOR.get(letter, DETAIL_FOR["c"]):
		var loaded = _load_detail(str(path))
		if loaded == null:
			continue
		# A texel of these levels is an inch, so this repeats the fine picture about every
		# metre and a quarter whatever size the wall's own texture is.
		var across := Vector2(texture.get_width(), texture.get_height()) / 48.0
		return {"texture": loaded["texture"], "mean": loaded["mean"], "scale": across,
			"amount": 0.7}
	return {}


func _load_detail(path: String):
	if _detail.has(path):
		return _detail[path]
	_detail[path] = null
	var wanted := "gfx/%s.tga" % path.to_lower()
	var hit: Dictionary = _lab.vfs.find_files("gfx/%s.tga" % path, PackedStringArray(["tga"]),
		PackedStringArray(), PackedStringArray(), 4)
	for uri in hit.get("files", PackedStringArray()):
		if not str(uri).to_lower().ends_with(wanted):
			continue
		var bytes: PackedByteArray = _lab.vfs.read_file(str(uri))
		var image := Image.new()
		if bytes.is_empty() or image.load_tga_from_buffer(bytes) != OK or image.get_width() <= 0:
			continue
		# Its average, so that multiplying by it neither darkens nor brightens the wall.
		var small: Image = image.duplicate()
		small.convert(Image.FORMAT_RGB8)
		small.resize(1, 1, Image.INTERPOLATE_LANCZOS)
		var average := small.get_pixel(0, 0)
		var mean := maxf((average.r + average.g + average.b) / 3.0, 0.05)
		image.generate_mipmaps()
		_detail[path] = {"texture": ImageTexture.create_from_image(image), "mean": mean}
		break
	return _detail[path]


# --------------------------------------------------------------------- shadows ----

## Who is standing in the sun, and how much.
##
## The sun's shadow is drawn only from what carries SUNLIT_LAYER. Somebody indoors is under a
## roof the sun's shadow knows nothing about, and would otherwise throw a shadow across a
## floor the sun never reaches.
##
## Not a switch. Walking out of a doorway the shadow comes up over a moment, and on the edge
## of a patch of sun it is a faint one: a body that is all at once fully shadowed is the thing
## the eye catches.
func _physics_process(delta: float) -> void:
	if not _on or _sun == null or _lab == null:
		return
	var floor_light = _lab.get("floor_light")
	if floor_light == null:
		return
	for entry in floor_light.get("_bodies"):
		if not is_instance_valid(entry.get("body")):
			continue
		# By the light painted on the floor under them, which is the one record of where the
		# sun reaches that the level carries. A ray toward the sun does not work: these levels
		# are roofed over with invisible walls that stop a player and a ray alike.
		var under: Color = entry.get("shown", Color.BLACK)
		var lit := maxf(under.r, maxf(under.g, under.b))
		var wanted := smoothstep(SUN_OFF, SUN_ON, lit)
		var now: float = entry.get("sun", 0.0)
		if is_equal_approx(now, wanted):
			continue
		_mark(entry, move_toward(now, wanted, delta * SUN_EASE))


## Give this body `sun` of a shadow, from 0 to 1.
func _mark(entry: Dictionary, sun: float) -> void:
	entry["sun"] = sun
	for mesh in entry.get("meshes", []):
		if not is_instance_valid(mesh):
			continue
		var instance := mesh as MeshInstance3D
		if sun > 0.01:
			instance.layers |= SUNLIT_LAYER
		else:
			instance.layers &= ~SUNLIT_LAYER
		instance.set_instance_shader_parameter("sun_fade", sun)


func _mark_all(sunlit: bool) -> void:
	if _lab == null or _lab.get("floor_light") == null:
		return
	for entry in _lab.get("floor_light").get("_bodies"):
		_mark(entry, 1.0 if sunlit else 0.0)
