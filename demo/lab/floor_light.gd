extends Node

## People lit by the floor they stand on.
##
## A GoldSrc level carries its light baked into its surfaces and has no lamps in it at all. A
## model is not baked, so the game asks what the light is on the ground under its feet and
## draws the whole model in that: bright in a doorway, dim in a cellar, tinted by a red lamp.
## Without it a body is lit by the sky alone and stands black in a room the level shows as
## well lit.
##
## The level's floors are gathered once, with where each sits in the level's light picture.
## After that a body is a look-up a few times a second, eased so it does not flicker on the
## seam between two surfaces.

## Floors are filed by the square they cover, this many metres across.
const CELL := 2.0

## How far down a floor is looked for, in metres. Past this somebody is in mid air.
const REACH := 6.0

## How fast a body takes up the light of where it now stands, per second.
const EASE := 6.0

## The level's own curve for turning a stored light value into what is shown, and a body
## shaded by which way each part of it faces: lit from above, never quite dark underneath.
const SHADER := """
shader_type spatial;
render_mode ambient_light_disabled, specular_disabled%s;
uniform sampler2D albedo_texture : filter_nearest_mipmap, repeat_enable, hint_default_white;
instance uniform vec3 floor_light = vec3(0.6);
// For the player's own figure: what hangs from the bones numbered from x to y is not drawn.
// Given the chest, head and arms, that leaves the legs and feet and takes away what the eye
// would otherwise be inside.
instance uniform vec2 hide_bones = vec2(-1.0);
// How much of a shadow this throws in the sun, from none to all of it. Only the sun's own
// picture of the level is drawn straight on rather than in perspective, which is how that
// picture is told from the one the eye sees; in it this much of the body is left out, in a
// fine scatter that the shadow's own blur turns into a fainter shadow.
instance uniform float sun_fade = 1.0;
varying float hidden;
void vertex() {
	float bone = float(BONE_INDICES.x);
	hidden = (hide_bones.x >= 0.0 && bone >= hide_bones.x && bone <= hide_bones.y) ? 1.0 : 0.0;
}
vec3 to_linear(vec3 c) {
	return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));
}
vec3 game_light(vec3 stored) {
	vec3 f = pow(min(stored * 1.3195, vec3(1.0)), vec3(2.5));
	float knee = 0.05;
	f = mix(f / knee * 0.125, 0.125 + (f - knee) / (1.0 - knee) * 0.875, step(vec3(knee), f));
	return pow(f, vec3(0.4));
}
void fragment() {
	if (hidden > 0.01) {
		discard;
	}
	if (sun_fade < 0.995 && PROJECTION_MATRIX[3][3] > 0.5) {
		float scatter = fract(52.9829189 * fract(dot(FRAGCOORD.xy, vec2(0.06711056, 0.00583715))));
		if (scatter >= sun_fade) {
			discard;
		}
	}
	vec4 tex = texture(albedo_texture, UV);
	vec3 up = (VIEW_MATRIX * vec4(0.0, 1.0, 0.0, 0.0)).xyz;
	float facing = dot(normalize(NORMAL), up) * 0.5 + 0.5;
	vec3 light = game_light(floor_light) * mix(0.6, 1.0, facing);
	vec3 base = pow(tex.rgb, vec3(0.8));
	// Given off, so it is what it was with nothing else lit; a lamp or a gunshot adds to it.
	EMISSION = to_linear(clamp(base * light, 0.0, 1.0));
	ALBEDO = to_linear(base);
	ROUGHNESS = 1.0;
	SPECULAR = 0.0;
%s}
void light() {
	if (!LIGHT_IS_DIRECTIONAL) {
		// Held down, so a weapon with the flash right on top of it is lit and not burnt white.
		DIFFUSE_LIGHT += min(clamp(dot(NORMAL, LIGHT), 0.0, 1.0) * ATTENUATION * LIGHT_COLOR / PI, vec3(0.6));
	}
}
"""

var _picture: Image
var _cells := {}
var _a := PackedVector3Array()
var _b := PackedVector3Array()
var _c := PackedVector3Array()
var _ua := PackedVector2Array()
var _ub := PackedVector2Array()
var _uc := PackedVector2Array()

var _plain: Shader
var _masked: Shader
var _made := {}
## Each body being lit: { body, meshes, shown }.
var _bodies: Array = []
var _next := 0


## Gather the floors of `level`. Says how many it found; none means the level carries no
## baked light and nothing here does anything.
func setup(level: Node3D) -> int:
	var stack: Array = [level]
	while not stack.is_empty():
		var node: Node = stack.pop_back()
		for child in node.get_children():
			stack.append(child)
		var instance := node as MeshInstance3D
		if instance == null or not (instance.mesh is ArrayMesh):
			continue
		var mesh := instance.mesh as ArrayMesh
		for s in mesh.get_surface_count():
			var material := mesh.surface_get_material(s) as ShaderMaterial
			if material == null:
				continue
			var light := material.get_shader_parameter("lightmap_texture") as Texture2D
			if light == null:
				continue
			if _picture == null:
				_picture = light.get_image()
			_gather(mesh.surface_get_arrays(s), instance.global_transform)
	if _picture != null and _picture.is_compressed():
		_picture.decompress()
	return _a.size()


func _gather(arrays: Array, place: Transform3D) -> void:
	var points: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
	var spots = arrays[Mesh.ARRAY_TEX_UV2]
	var order = arrays[Mesh.ARRAY_INDEX]
	if spots == null or (spots as PackedVector2Array).is_empty():
		return
	var count: int = (order as PackedInt32Array).size() if order != null else points.size()
	for i in range(0, count - 2, 3):
		var ia: int = order[i] if order != null else i
		var ib: int = order[i + 1] if order != null else i + 1
		var ic: int = order[i + 2] if order != null else i + 2
		var a := place * points[ia]
		var b := place * points[ib]
		var c := place * points[ic]
		# Something to stand on, whichever way round it was wound.
		var normal := (b - a).cross(c - a)
		if normal.length() < 0.000001 or absf(normal.normalized().y) < 0.7:
			continue
		var index := _a.size()
		_a.append(a)
		_b.append(b)
		_c.append(c)
		_ua.append(spots[ia])
		_ub.append(spots[ib])
		_uc.append(spots[ic])
		var low := Vector2(minf(a.x, minf(b.x, c.x)), minf(a.z, minf(b.z, c.z)))
		var high := Vector2(maxf(a.x, maxf(b.x, c.x)), maxf(a.z, maxf(b.z, c.z)))
		for x in range(floori(low.x / CELL), floori(high.x / CELL) + 1):
			for z in range(floori(low.y / CELL), floori(high.y / CELL) + 1):
				var key := Vector2i(x, z)
				# Taken out and put back: a packed array in a dictionary is a copy, and adding
				# to the copy leaves the one in the dictionary empty.
				var filed: PackedInt32Array = _cells.get(key, PackedInt32Array())
				filed.append(index)
				_cells[key] = filed


## The level's stored light on the floor under `at`, or null when there is no floor there.
func sample(at: Vector3) -> Variant:
	if _picture == null:
		return null
	var here = _cells.get(Vector2i(floori(at.x / CELL), floori(at.z / CELL)))
	if here == null:
		return null
	var best := -INF
	var spot := Vector2.ZERO
	for index in (here as PackedInt32Array):
		var a := _a[index]
		var b := _b[index]
		var c := _c[index]
		# Where `at` falls inside the triangle, seen from above.
		var d := (b.z - c.z) * (a.x - c.x) + (c.x - b.x) * (a.z - c.z)
		if absf(d) < 0.0000001:
			continue
		var u := ((b.z - c.z) * (at.x - c.x) + (c.x - b.x) * (at.z - c.z)) / d
		var v := ((c.z - a.z) * (at.x - c.x) + (a.x - c.x) * (at.z - c.z)) / d
		var w := 1.0 - u - v
		if u < -0.001 or v < -0.001 or w < -0.001:
			continue
		var height := a.y * u + b.y * v + c.y * w
		# The nearest one beneath, not the roof and not the floor of the storey below.
		if height > at.y + 0.1 or height < at.y - REACH or height <= best:
			continue
		best = height
		spot = _ua[index] * u + _ub[index] * v + _uc[index] * w
	if best == -INF:
		return null
	var x := clampi(int(spot.x * _picture.get_width()), 0, _picture.get_width() - 1)
	var y := clampi(int(spot.y * _picture.get_height()), 0, _picture.get_height() - 1)
	return _picture.get_pixel(x, y)


## Start lighting `body` by the floor. `feet` is how far below its origin its soles are.
func dress(body: Node3D, feet := 0.0) -> void:
	if _picture == null:
		return
	var meshes: Array = []
	var stack: Array = [body]
	while not stack.is_empty():
		var node: Node = stack.pop_back()
		for child in node.get_children():
			stack.append(child)
		var instance := node as MeshInstance3D
		if instance == null or instance.mesh == null or instance.name == "Shadow":
			continue
		var changed := false
		for s in instance.mesh.get_surface_count():
			var old := instance.get_active_material(s) as BaseMaterial3D
			if old == null or old.albedo_texture == null:
				continue
			instance.set_surface_override_material(s, _material_for(old))
			changed = true
		if changed:
			meshes.append(instance)
	if meshes.is_empty():
		return
	var entry := {"body": body, "meshes": meshes, "feet": feet, "shown": Color(0.6, 0.6, 0.6)}
	var found = sample(body.global_position - Vector3(0, feet - 0.05, 0))
	if found != null:
		entry["shown"] = found
	_apply(entry)
	_bodies.append(entry)


func _material_for(old: BaseMaterial3D) -> ShaderMaterial:
	var key := old.get_instance_id()
	if _made.has(key):
		return _made[key]
	var cut := old.transparency != BaseMaterial3D.TRANSPARENCY_DISABLED
	var both := old.cull_mode == BaseMaterial3D.CULL_DISABLED
	var material := ShaderMaterial.new()
	material.shader = _shader(cut, both)
	material.set_shader_parameter("albedo_texture", old.albedo_texture)
	_made[key] = material
	return material


func _shader(cut: bool, both: bool) -> Shader:
	if not both:
		if cut and _masked != null:
			return _masked
		if not cut and _plain != null:
			return _plain
	var shader := Shader.new()
	shader.code = SHADER % [", cull_disabled" if both else "",
		"\tALPHA = tex.a;\n\tALPHA_SCISSOR_THRESHOLD = 0.5;\n" if cut else ""]
	if not both:
		if cut:
			_masked = shader
		else:
			_plain = shader
	return shader


func _apply(entry: Dictionary) -> void:
	var shown: Color = entry["shown"]
	for mesh in entry["meshes"]:
		if is_instance_valid(mesh):
			(mesh as MeshInstance3D).set_instance_shader_parameter("floor_light",
				Vector3(shown.r, shown.g, shown.b))


func _process(delta: float) -> void:
	if _bodies.is_empty():
		return
	# One look-up a frame, taken in turn: nobody's light changes faster than they can walk.
	_next = (_next + 1) % _bodies.size()
	var entry: Dictionary = _bodies[_next]
	var body = entry["body"]
	if not is_instance_valid(body):
		_bodies.remove_at(_next)
		return
	var found = sample((body as Node3D).global_position - Vector3(0, float(entry["feet"]) - 0.05, 0))
	if found != null:
		entry["wanted"] = found
	if not entry.has("wanted"):
		return
	var step := clampf(delta * _bodies.size() * EASE, 0.0, 1.0)
	entry["shown"] = (entry["shown"] as Color).lerp(entry["wanted"] as Color, step)
	_apply(entry)
