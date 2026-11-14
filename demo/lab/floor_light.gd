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
##
## A Source level does better and keeps the light for moving things itself: spots all
## through its open space, each with the light falling from six directions. Given that
## (use_ambient), a body is lit by it instead, each part by the way it faces, and the props
## the level places are lit by it too, once, where they stand.

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
// A Source level's light for moving things: what falls on a surface facing +X, -X, +Y, -Y,
// +Z and -Z, linear, as the level stored it.
uniform bool cube_light = false;
instance uniform vec3 light_px = vec3(0.6);
instance uniform vec3 light_nx = vec3(0.6);
instance uniform vec3 light_py = vec3(0.6);
instance uniform vec3 light_ny = vec3(0.6);
instance uniform vec3 light_pz = vec3(0.6);
instance uniform vec3 light_nz = vec3(0.6);
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
	if (cube_light) {
		// The six directions mixed by how much this part faces each of them.
		vec3 n = normalize((INV_VIEW_MATRIX * vec4(NORMAL, 0.0)).xyz);
		vec3 share = n * n;
		vec3 lit = share.x * (n.x >= 0.0 ? light_px : light_nx)
			+ share.y * (n.y >= 0.0 ? light_py : light_ny)
			+ share.z * (n.z >= 0.0 ? light_pz : light_nz);
		// As the level's own walls are drawn: the painted colour, linear, times the light.
		vec3 base = to_linear(tex.rgb);
		EMISSION = base * lit;
		ALBEDO = base;
	} else {
		vec3 up = (VIEW_MATRIX * vec4(0.0, 1.0, 0.0, 0.0)).xyz;
		float facing = dot(normalize(NORMAL), up) * 0.5 + 0.5;
		vec3 light = game_light(floor_light) * mix(0.6, 1.0, facing);
		vec3 base = pow(tex.rgb, vec3(0.8));
		// Given off, so it is what it was with nothing else lit; a lamp or a gunshot adds to it.
		EMISSION = to_linear(clamp(base * light, 0.0, 1.0));
		ALBEDO = to_linear(base);
	}
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
## Each body being lit: { body, meshes, feet, shown }, and with a Source level's light also
## the six colours it is shown in ("cube").
var _bodies: Array = []
var _next := 0

## A Source level's light for moving things, as load_map_ambient() gives it.
var _cube := false
var _root := 0
var _splits := PackedFloat32Array()
var _children := PackedInt32Array()
var _leaves := PackedInt32Array()
var _spots := PackedVector3Array()
var _colours := PackedColorArray()

## The six names in the shader, in the order the colours come.
const CUBE_NAMES := ["light_px", "light_nx", "light_py", "light_ny", "light_pz", "light_nz"]

## A level's light for moving things is refused when its brightest is this many times
## darker than the well lit part of its walls (the brightest tenth of the light picture).
const AMBIENT_FLOOR := 20.0

## Why the level's light for moving things was not used, when it was not.
var ambient_refused := ""

## How far above the soles a body's light is taken, in metres: low enough to be under any
## ceiling, high enough not to be under the floor.
const CUBE_HEIGHT := 0.3


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


## Light bodies and props by a Source level's own light for moving things, as
## load_map_ambient() gives it, rather than by the floor. Says how many spots it holds; none
## leaves the floor in charge.
func use_ambient(data: Dictionary) -> int:
	ambient_refused = ""
	if data.is_empty() or (data.get("spots", PackedVector3Array()) as PackedVector3Array).is_empty():
		return 0
	# Some levels keep only a trace of it: CS:GO's own, it seems, which light their models
	# with the sun as it is drawn rather than with what the level stored. Shipped keeps
	# nothing brighter than 0.006 while its walls reach past 1. Lit by that, everything
	# stands black, so the floor is kept in charge.
	var brightest := 0.0
	for c in (data["colours"] as PackedColorArray):
		brightest = maxf(brightest, maxf(c.r, maxf(c.g, c.b)))
	var walls := _bright_walls()
	if walls > 0.0 and brightest * AMBIENT_FLOOR < walls:
		ambient_refused = "its brightest is %.4f against %.2f on its walls" % [brightest, walls]
		return 0
	_cube = true
	_root = int(data["root"])
	_splits = data["splits"]
	_children = data["children"]
	_leaves = data["leaves"]
	_spots = data["spots"]
	_colours = data["colours"]
	return _spots.size()


## How bright the well lit part of the level's walls is: the light picture's brightest
## tenth, read from a few thousand of its pixels. 0 when there is no picture.
func _bright_walls() -> float:
	if _picture == null:
		return 0.0
	var w := _picture.get_width()
	var h := _picture.get_height()
	var seen := PackedFloat32Array()
	for i in 4096:
		# Spread over the picture without a pattern that lines up with its blocks.
		var x := int(fposmod(i * 0.618034, 1.0) * w)
		var y := int(fposmod(i * 0.754878, 1.0) * h)
		var c := _picture.get_pixel(x, y)
		seen.append(maxf(c.r, maxf(c.g, c.b)))
	seen.sort()
	return seen[int(seen.size() * 0.9)]


## The six colours at `at` in a Source level, or null where it kept none (inside a wall).
func light_at(at: Vector3) -> Variant:
	if not _cube:
		return null
	# Down the tree of planes to the piece of open space `at` is in.
	var node := _root
	var steps := 0
	var most := _children.size()
	while node >= 0:
		if node * 2 + 1 >= most or steps > most:
			return null
		var i := node * 4
		var ahead := _splits[i] * at.x + _splits[i + 1] * at.y + _splits[i + 2] * at.z - _splits[i + 3]
		node = _children[node * 2] if ahead >= 0.0 else _children[node * 2 + 1]
		steps += 1
	var leaf := -(node + 1)
	if leaf * 2 + 1 >= _leaves.size():
		return null
	var first := _leaves[leaf * 2]
	var count := _leaves[leaf * 2 + 1]
	if count == 0:
		return null
	# Its spots, the nearer counting for more, so the light changes smoothly across it.
	var out := PackedColorArray([Color.BLACK, Color.BLACK, Color.BLACK, Color.BLACK, Color.BLACK, Color.BLACK])
	var total := 0.0
	for s in range(first, first + count):
		var weight := 1.0 / (_spots[s].distance_squared_to(at) + 0.01)
		total += weight
		for f in 6:
			out[f] += _colours[s * 6 + f] * weight
	for f in 6:
		var c := out[f] / total
		c.a = 1.0
		out[f] = c
	return out


## Light one of the level's props by where it stands. Done once: they do not move.
func light_prop(instance: MeshInstance3D) -> void:
	if not _cube or instance.mesh == null:
		return
	var found = light_at(instance.global_transform * instance.mesh.get_aabb().get_center())
	if found == null:
		# Its middle is inside a wall or under the floor; a little above its origin is not.
		found = light_at(instance.global_transform.origin + Vector3(0, CUBE_HEIGHT, 0))
	if found == null:
		return
	var over := instance.material_override as BaseMaterial3D
	if over != null:
		if over.albedo_texture == null:
			return
		instance.material_override = _material_for(over)
	else:
		for s in instance.mesh.get_surface_count():
			var old := instance.mesh.surface_get_material(s) as BaseMaterial3D
			if old != null and old.albedo_texture != null:
				instance.set_surface_override_material(s, _material_for(old))
	_apply_cube(instance, found)


func _apply_cube(instance: GeometryInstance3D, cube: PackedColorArray) -> void:
	for f in 6:
		var c := cube[f]
		instance.set_instance_shader_parameter(CUBE_NAMES[f], Vector3(c.r, c.g, c.b))


## Where a body's light is looked up, and what it is there: a colour from the floor, or a
## Source level's six. Null when there is nothing there.
func _look(body: Node3D, feet: float) -> Variant:
	if _cube:
		return light_at(body.global_position - Vector3(0, feet - CUBE_HEIGHT, 0))
	return sample(body.global_position - Vector3(0, feet - 0.05, 0))


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
	if _picture == null and not _cube:
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
	var found = _look(body, feet)
	if found is PackedColorArray:
		entry["cube"] = found
		entry["shown"] = (found as PackedColorArray)[2]
	elif found != null:
		entry["shown"] = found
	elif _cube:
		var grey := Color(0.6, 0.6, 0.6)
		entry["cube"] = PackedColorArray([grey, grey, grey, grey, grey, grey])
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
	material.set_shader_parameter("cube_light", _cube)
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
		if not is_instance_valid(mesh):
			continue
		if entry.has("cube"):
			_apply_cube(mesh as MeshInstance3D, entry["cube"])
		else:
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
	var found = _look(body as Node3D, float(entry["feet"]))
	if found != null:
		entry["wanted"] = found
	if not entry.has("wanted"):
		return
	var step := clampf(delta * _bodies.size() * EASE, 0.0, 1.0)
	if entry.has("cube"):
		var shown: PackedColorArray = entry["cube"]
		var wanted: PackedColorArray = entry["wanted"]
		for f in 6:
			shown[f] = shown[f].lerp(wanted[f], step)
		entry["cube"] = shown
		entry["shown"] = shown[2]
	else:
		entry["shown"] = (entry["shown"] as Color).lerp(entry["wanted"] as Color, step)
	_apply(entry)
