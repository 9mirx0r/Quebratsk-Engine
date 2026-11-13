@tool
extends RefCounted
## Land beyond the edge of an imported level: hills, mountains, dunes or a town, kilometres
## of it, fading into the sky.
##
## A GoldSrc level ends at its walls, and past them there is only a painted sky. That is why
## the old games feel like a box. This puts real ground out there, so that looking over a
## wall shows distance: near slopes are crisp and coloured, far ones are pale, and the last
## ridge is the colour of the horizon.
##
##   const DistantBackdrop := preload("res://addons/quebratsk_editor/distant_backdrop.gd")
##   var sky := ProceduralSky.attach(map, entities)
##   DistantBackdrop.attach(map, "mountains", sky)
##
## It adds one "DistantBackdrop" node under the map. The land starts a little outside the
## level, flat and at the height of the level's lowest floor, and rises with distance out to
## REACH metres. A camera has to see that far: set its `far` to REACH or more.
##
## Only the backdrop is hazed. The level itself is left exactly as it was.

const SHADER := preload("res://addons/quebratsk_editor/distant_backdrop.gdshader")

## How far the land goes, in metres.
const REACH := 7000.0
const RINGS := 96
const SPOKES := 360
## Trees stand on the land out to this many metres from the level; past it the woods are
## only a darker colour of the ground.
const TREE_REACH := 1500.0
## Where the photographs of ground, building fronts and trees are kept.
const ART := "res://addons/quebratsk_editor/backdrop/"
## How strongly a photograph is pulled towards its style's own colour: 0 leaves it as it was
## taken, 1 makes it exactly that colour on average.
const TINT := 0.6

## What each kind of land is made of. `relief` is the height of the tallest ground in
## metres, `grain` the size of its largest features, `ridged` whether it comes to sharp
## crests, `lead` how many metres of nearly flat ground lie between level and slopes,
## `patches` how much of the low ground is wooded and `tree` what stands there.
## `ground` names the photographs for low ground, high ground, woods from above, steep
## ground and peaks, and `tree_picture` the one of a single tree.
const STYLES := {
	"hills": {
		"ground": ["pasto", "pasto_seco", "bosque_desde_arriba", "tierra", "nieve"], "tree_picture": "arbol_hoja",
		"relief": 320.0, "grain": 1100.0, "ridged": false, "lead": 200.0, "patches": 0.55, "tree": "round",
		"low": Color(0.27, 0.36, 0.17), "high": Color(0.36, 0.38, 0.22),
		"steep": Color(0.34, 0.31, 0.25), "peak": Color.WHITE, "peak_line": 2.0,
	},
	"mountains": {
		"ground": ["pasto", "piedras_sueltas", "bosque_pinos_desde_arriba", "roca", "nieve"], "tree_picture": "arbol_pino",
		"relief": 1500.0, "grain": 3200.0, "ridged": true, "lead": 500.0, "patches": 0.6, "tree": "pine",
		"low": Color(0.25, 0.33, 0.18), "high": Color(0.38, 0.36, 0.33),
		"steep": Color(0.33, 0.32, 0.31), "peak": Color(0.93, 0.95, 0.98), "peak_line": 0.62,
	},
	"desert": {
		"ground": ["arena", "desierto_pedregoso", "pasto_seco", "roca", "arena"], "tree_picture": "arbol_seco",
		"relief": 260.0, "grain": 1400.0, "ridged": true, "lead": 300.0, "patches": 0.12, "tree": "scrub",
		"low": Color(0.71, 0.58, 0.39), "high": Color(0.66, 0.47, 0.31),
		"steep": Color(0.52, 0.34, 0.23), "peak": Color.WHITE, "peak_line": 2.0,
	},
	"city": {
		"ground": ["asfalto", "pasto", "bosque_desde_arriba", "roca", "nieve"], "tree_picture": "arbol_hoja",
		"relief": 60.0, "grain": 1800.0, "ridged": false, "lead": 2500.0, "patches": 0.3, "tree": "round",
		"low": Color(0.30, 0.31, 0.30), "high": Color(0.33, 0.36, 0.26),
		"steep": Color(0.36, 0.36, 0.37), "peak": Color.WHITE, "peak_line": 2.0,
	},
}


## Put land of the given style (a key of STYLES) around `map`. `sky_node` is what
## ProceduralSky.attach() returned, if there is one: the land then takes its sun and its
## horizon colour from it. `seed` picks a different landscape of the same kind.
## Returns the node that was added, or null for a style that does not exist.
static func attach(map: Node3D, style: String = "hills", sky_node: Node = null, seed: int = 1) -> Node3D:
	if not STYLES.has(style):
		push_error("DistantBackdrop: no style called '%s'" % style)
		return null
	var look: Dictionary = STYLES[style]
	var box := bounds_of(map)
	var centre := Vector3(box.get_center().x, box.position.y, box.get_center().z)
	var inner := Vector2(box.size.x, box.size.z).length() * 0.5 + 20.0

	var material := ShaderMaterial.new()
	material.shader = SHADER
	material.set_shader_parameter(&"low_color", look["low"])
	material.set_shader_parameter(&"high_color", look["high"])
	material.set_shader_parameter(&"steep_color", look["steep"])
	material.set_shader_parameter(&"peak_color", look["peak"])
	material.set_shader_parameter(&"peak_line", look["peak_line"])
	material.set_shader_parameter(&"patches", look["patches"])
	var wood_color := Color(0.42, 0.40, 0.24) if style == "desert" else Color(0.13, 0.21, 0.10)
	material.set_shader_parameter(&"patch_color", wood_color)
	_dress(material, look, wood_color)
	material.set_shader_parameter(&"base_height", centre.y)
	material.set_shader_parameter(&"relief", look["relief"])
	var detail := _detail()
	material.set_shader_parameter(&"detail_noise", ImageTexture.create_from_image(detail))
	_match_sky(material, sky_node)

	var root := Node3D.new()
	root.name = "DistantBackdrop"
	root.set_meta(&"style", style)
	root.set_meta(&"inner_radius", inner)

	var noise := FastNoiseLite.new()
	noise.seed = seed
	noise.noise_type = FastNoiseLite.TYPE_SIMPLEX_SMOOTH
	noise.fractal_type = FastNoiseLite.FRACTAL_RIDGED if look["ridged"] else FastNoiseLite.FRACTAL_FBM
	noise.fractal_octaves = 5
	noise.frequency = 1.0 / float(look["grain"])

	var land := MeshInstance3D.new()
	land.name = "Land"
	var heights := PackedFloat32Array()
	land.mesh = _land(centre, inner, look, noise, heights)
	land.material_override = material
	land.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	# One piece all around the level: never to be dropped for being "behind" the camera.
	land.custom_aabb = AABB(Vector3(-REACH, -100.0, -REACH), Vector3(REACH * 2.0, 4000.0, REACH * 2.0))
	land.position = centre
	root.add_child(land)

	var trees := _trees(centre, inner, look, heights, detail, seed, material.get_meta(&"tree_shape", 0.7))
	trees.material_override = material
	trees.set_instance_shader_parameter(&"canopy", 1.0)
	root.add_child(trees)

	if style == "city":
		var town := _town(centre, inner, seed)
		town.material_override = material
		town.set_instance_shader_parameter(&"canopy", 2.0)
		var fronts: Array[Image] = []
		for front in ["fachada_monoblock", "fachada_ladrillo", "fachada_oficinas", "fachada_revoque", "techo"]:
			fronts.append(_picture(front + ".jpg", Color(0.4, 0.4, 0.42)))
		var town_layers := Texture2DArray.new()
		town_layers.create_from_images(fronts)
		material.set_shader_parameter(&"town_layers", town_layers)
		root.add_child(town)

	map.add_child(root)
	return root


## The backdrop's material, for changing its colours or its haze:
##   DistantBackdrop.material_of(node).set_shader_parameter("haze_distance", 1500.0)
static func material_of(backdrop: Node) -> ShaderMaterial:
	var land := backdrop.get_node_or_null("Land") as MeshInstance3D
	return land.material_override as ShaderMaterial if land != null else null


## The box that holds everything visible in a level, in the level's own space.
static func bounds_of(map: Node3D) -> AABB:
	var found := [false, AABB()]
	_gather(map, Transform3D.IDENTITY, found)
	return found[1] if found[0] else AABB(Vector3(-50, 0, -50), Vector3(100, 20, 100))


static func _gather(node: Node, to_map: Transform3D, found: Array) -> void:
	var mesh_instance := node as MeshInstance3D
	if mesh_instance != null and mesh_instance.mesh != null and node.name != &"Sky":
		var box: AABB = to_map * mesh_instance.mesh.get_aabb()
		found[1] = found[1].merge(box) if found[0] else box
		found[0] = true
	for child in node.get_children():
		# The painted sky box is as big as the camera can see and says nothing about the level.
		if child is Node3D and child.name != &"Sky" and child.name != &"DistantBackdrop":
			_gather(child, to_map * (child as Node3D).transform, found)


## How high the land is at a distance `r` from the level's centre, above the flat ground.
static func height_at(x: float, z: float, r: float, inner: float, look: Dictionary, noise: FastNoiseLite) -> float:
	var lead: float = look["lead"]
	# Flat by the level, then rising, and tallest far away, where it reads as a skyline.
	var rise := smoothstep(inner, inner + lead, r) * lerpf(0.18, 1.0, smoothstep(inner, REACH * 0.55, r))
	var n := noise.get_noise_2d(x, z) * 0.5 + 0.5
	return rise * n * float(look["relief"])


## `heights` is filled with the height of every corner of the grid, ring by ring.
static func _land(centre: Vector3, inner: float, look: Dictionary, noise: FastNoiseLite, heights: PackedFloat32Array) -> ArrayMesh:
	var points := PackedVector3Array()
	var normals := PackedVector3Array()
	var indices := PackedInt32Array()
	points.resize(RINGS * SPOKES)
	normals.resize(RINGS * SPOKES)
	heights.resize(RINGS * SPOKES)
	for ring in RINGS:
		# Rings crowd together near the level, where a slope is seen close up.
		var r := inner * pow(REACH / inner, float(ring) / float(RINGS - 1))
		var reach := maxf(r * TAU / float(SPOKES), 4.0)
		for spoke in SPOKES:
			var angle := TAU * float(spoke) / float(SPOKES)
			var x := cos(angle) * r
			var z := sin(angle) * r
			var wx := centre.x + x
			var wz := centre.z + z
			var h := height_at(wx, wz, r, inner, look, noise)
			var hx := height_at(wx + reach, wz, Vector2(x + reach, z).length(), inner, look, noise)
			var hz := height_at(wx, wz + reach, Vector2(x, z + reach).length(), inner, look, noise)
			var at := ring * SPOKES + spoke
			points[at] = Vector3(x, h, z)
			heights[at] = h
			normals[at] = Vector3(h - hx, reach, h - hz).normalized()
	for ring in RINGS - 1:
		for spoke in SPOKES:
			var a := ring * SPOKES + spoke
			var b := ring * SPOKES + (spoke + 1) % SPOKES
			var c := a + SPOKES
			var d := b + SPOKES
			indices.append_array([a, c, b, b, c, d])
	# The ground under the level itself, a touch lower than its lowest floor.
	var middle := points.size()
	points.append(Vector3(0.0, -1.0, 0.0))
	normals.append(Vector3.UP)
	for spoke in SPOKES:
		indices.append_array([middle, spoke, (spoke + 1) % SPOKES])
	var arrays := []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = points
	arrays[Mesh.ARRAY_NORMAL] = normals
	arrays[Mesh.ARRAY_INDEX] = indices
	var mesh := ArrayMesh.new()
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
	return mesh


## The picture of soft blotches the shader reads. It repeats without a seam.
static var _pictures := {}


## One of the photographs in ART, square and ready to be a layer. If it is missing, a flat
## picture of `instead` stands in for it, so the land is still drawn, only plainer.
static func _picture(file: String, instead: Color) -> Image:
	if _pictures.has(file):
		return _pictures[file]
	var image := Image.new()
	var bytes := FileAccess.get_file_as_bytes(ART + file)
	var failed := true
	if not bytes.is_empty():
		failed = (image.load_png_from_buffer(bytes) if file.ends_with(".png") else image.load_jpg_from_buffer(bytes)) != OK
	if failed:
		image = Image.create(512, 512, false, Image.FORMAT_RGBA8)
		image.fill(instead)
	if file.ends_with(".png"):
		image.convert(Image.FORMAT_RGBA8)
	else:
		image.convert(Image.FORMAT_RGB8)
		if image.get_size() != Vector2i(512, 512):
			image.resize(512, 512)
	image.generate_mipmaps()
	_pictures[file] = image
	return image


## What colour a photograph is on the whole, counting only what is not see-through.
static func _average(image: Image) -> Color:
	var sum := Vector3.ZERO
	var counted := 0.0
	for i in 24:
		for j in 24:
			var pixel := image.get_pixel(i * image.get_width() / 24, j * image.get_height() / 24)
			if pixel.a < 0.5:
				continue
			pixel = pixel.srgb_to_linear()
			sum += Vector3(pixel.r, pixel.g, pixel.b)
			counted += 1.0
	sum /= maxf(counted, 1.0)
	return Color(sum.x, sum.y, sum.z)


## What a photograph has to be multiplied by to come out, on average, nearer to `wanted`.
static func _tint(image: Image, wanted: Color) -> Vector3:
	var has := _average(image)
	var aim := wanted.srgb_to_linear()
	return Vector3(
		lerpf(1.0, aim.r / maxf(has.r, 0.01), TINT),
		lerpf(1.0, aim.g / maxf(has.g, 0.01), TINT),
		lerpf(1.0, aim.b / maxf(has.b, 0.01), TINT))


## Give the material its photographs of ground and of a tree, each tinted to the style.
static func _dress(material: ShaderMaterial, look: Dictionary, wood_color: Color) -> void:
	var wanted: Array[Color] = [look["low"], look["high"], wood_color, look["steep"], look["peak"]]
	var layers: Array[Image] = []
	var tints := PackedVector3Array()
	for i in 5:
		var image := _picture(str(look["ground"][i]) + ".jpg", wanted[i])
		layers.append(image)
		tints.append(_tint(image, wanted[i]))
	var ground := Texture2DArray.new()
	ground.create_from_images(layers)
	material.set_shader_parameter(&"ground_layers", ground)
	material.set_shader_parameter(&"layer_tint", tints)

	var tree := _picture(str(look["tree_picture"]) + ".png", wood_color)
	material.set_shader_parameter(&"tree_picture", ImageTexture.create_from_image(tree))
	material.set_shader_parameter(&"tree_tint", _tint(tree, wood_color.lightened(0.25)))
	material.set_meta(&"tree_shape", float(tree.get_width()) / float(tree.get_height()))


static func _detail() -> Image:
	var noise := FastNoiseLite.new()
	noise.seed = 5
	noise.noise_type = FastNoiseLite.TYPE_SIMPLEX_SMOOTH
	noise.fractal_type = FastNoiseLite.FRACTAL_FBM
	noise.fractal_octaves = 4
	noise.frequency = 0.012
	var picture := noise.get_seamless_image(512, 512, false, false, 0.15)
	picture.generate_mipmaps()
	return picture


## What the shader reads from the picture at a spot on the ground, `across` metres to a repeat.
static func _blotch(detail: Image, x: float, z: float, across: float, shift: Vector2) -> float:
	var size := detail.get_width()
	var u := (x / across + shift.x) * size - 0.5
	var v := (z / across + shift.y) * size - 0.5
	var u0 := floori(u)
	var v0 := floori(v)
	var fu := u - u0
	var fv := v - v0
	var a := detail.get_pixel(posmod(u0, size), posmod(v0, size)).r
	var b := detail.get_pixel(posmod(u0 + 1, size), posmod(v0, size)).r
	var c := detail.get_pixel(posmod(u0, size), posmod(v0 + 1, size)).r
	var d := detail.get_pixel(posmod(u0 + 1, size), posmod(v0 + 1, size)).r
	return lerpf(lerpf(a, b, fu), lerpf(c, d, fu), fv)


## How wooded a spot is, by the same sum the shader colours the ground with.
static func stands_at(detail: Image, x: float, z: float) -> float:
	return _blotch(detail, x, z, 2600.0, Vector2.ZERO) * 0.62 \
			+ _blotch(detail, x, z, 410.0, Vector2(0.3, 0.7)) * 0.28 \
			+ _blotch(detail, x, z, 61.0, Vector2(0.6, 0.2)) * 0.10


## The height of the land as it is drawn (not as the noise has it) at a spot `x`, `z` from
## the level's centre.
static func _drawn_height(heights: PackedFloat32Array, inner: float, x: float, z: float) -> float:
	var r := clampf(Vector2(x, z).length(), inner, REACH)
	var ring := log(r / inner) / log(REACH / inner) * float(RINGS - 1)
	var spoke := fposmod(atan2(z, x), TAU) / TAU * float(SPOKES)
	var r0 := mini(floori(ring), RINGS - 2)
	var s0 := floori(spoke) % SPOKES
	var s1 := (s0 + 1) % SPOKES
	var v := ring - r0
	var u := spoke - floorf(spoke)
	var a := heights[r0 * SPOKES + s0]
	var b := heights[r0 * SPOKES + s1]
	var c := heights[(r0 + 1) * SPOKES + s0]
	var d := heights[(r0 + 1) * SPOKES + s1]
	if u + v <= 1.0:
		return a + u * (b - a) + v * (c - a)
	return d + (1.0 - u) * (c - d) + (1.0 - v) * (b - d)


## Trees where the ground is wooded, near enough to the level to be seen as trees: thick
## close by, thinning with distance.
## Two upright squares through each other, one metre across: a tree's picture, seen from any side.
static func _crossed() -> ArrayMesh:
	var points := PackedVector3Array()
	var ups := PackedVector3Array()
	var spots := PackedVector2Array()
	var order := PackedInt32Array()
	for turn in 2:
		var along := Vector3(1.0, 0.0, 0.0) if turn == 0 else Vector3(0.0, 0.0, 1.0)
		var first := points.size()
		for corner in [Vector2(-0.5, -0.5), Vector2(0.5, -0.5), Vector2(0.5, 0.5), Vector2(-0.5, 0.5)]:
			points.append(along * corner.x + Vector3.UP * corner.y)
			ups.append(Vector3.UP)
			spots.append(Vector2(corner.x + 0.5, 0.5 - corner.y))
		order.append_array([first, first + 1, first + 2, first, first + 2, first + 3])
	var arrays := []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = points
	arrays[Mesh.ARRAY_NORMAL] = ups
	arrays[Mesh.ARRAY_TEX_UV] = spots
	arrays[Mesh.ARRAY_INDEX] = order
	var mesh := ArrayMesh.new()
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
	return mesh


static func _trees(centre: Vector3, inner: float, look: Dictionary, heights: PackedFloat32Array, detail: Image, seed: int, shape: float) -> MultiMeshInstance3D:
	var random := RandomNumberGenerator.new()
	random.seed = seed + 23
	var kind: String = look["tree"]
	var edge := 0.72 - float(look["patches"]) * 0.3 + 0.025
	var relief: float = look["relief"]
	const STEP := 10.0

	var places: Array[Transform3D] = []
	var across := int(TREE_REACH / STEP)
	for i in range(-across, across):
		for j in range(-across, across):
			var x := (i + random.randf()) * STEP
			var z := (j + random.randf()) * STEP
			var grow := random.randf()
			var r := Vector2(x, z).length()
			if r < inner + 12.0 or r > TREE_REACH:
				continue
			if grow > lerpf(1.0, 0.3, smoothstep(250.0, TREE_REACH, r)):
				continue
			if stands_at(detail, centre.x + x, centre.z + z) < edge:
				continue
			var ground := _drawn_height(heights, inner, x, z)
			if ground > relief * 0.5:
				continue
			var tall := lerpf(11.0, 18.0, random.randf())
			if kind == "pine":
				tall = lerpf(14.0, 24.0, random.randf())
			elif kind == "scrub":
				tall = lerpf(2.5, 4.5, random.randf())
			var wide := tall * shape
			var basis := Basis(Vector3.UP, random.randf() * TAU).scaled(Vector3(wide, tall, wide))
			places.append(Transform3D(basis, Vector3(centre.x + x, centre.y + ground + tall * 0.47, centre.z + z)))

	var wood := MultiMesh.new()
	wood.transform_format = MultiMesh.TRANSFORM_3D
	wood.mesh = _crossed()
	wood.instance_count = places.size()
	for i in places.size():
		wood.set_instance_transform(i, places[i])
	var trees := MultiMeshInstance3D.new()
	trees.name = "Trees"
	trees.multimesh = wood
	# Kept beside the mesh so that the trees can be found again without asking the renderer.
	var spots := PackedVector3Array()
	for place in places:
		spots.append(place.origin)
	trees.set_meta(&"spots", spots)
	trees.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	return trees


## A town of plain blocks around the level: low ones near, towers in clumps further out.
static func _town(centre: Vector3, inner: float, seed: int) -> MultiMeshInstance3D:
	var random := RandomNumberGenerator.new()
	random.seed = seed
	var clumps := FastNoiseLite.new()
	clumps.seed = seed + 11
	clumps.frequency = 1.0 / 600.0

	var places: Array[Transform3D] = []
	for i in 2600:
		var r := inner + 60.0 + pow(random.randf(), 1.6) * 2300.0
		var angle := random.randf() * TAU
		var x := centre.x + cos(angle) * r
		var z := centre.z + sin(angle) * r
		var downtown := clampf(clumps.get_noise_2d(x, z) * 1.6 + 0.35, 0.0, 1.0)
		var tall := lerpf(8.0, 26.0, random.randf()) + downtown * downtown * random.randf() * 150.0
		var wide := lerpf(14.0, 34.0, random.randf())
		var deep := lerpf(14.0, 34.0, random.randf())
		var basis := Basis(Vector3.UP, snappedf(random.randf() * TAU, PI * 0.5) + 0.2)
		places.append(Transform3D(basis.scaled(Vector3(wide, tall, deep)), Vector3(x, centre.y + tall * 0.5, z)))

	var blocks := MultiMesh.new()
	blocks.transform_format = MultiMesh.TRANSFORM_3D
	blocks.mesh = BoxMesh.new()
	blocks.instance_count = places.size()
	for i in places.size():
		blocks.set_instance_transform(i, places[i])
	var town := MultiMeshInstance3D.new()
	town.name = "Town"
	town.multimesh = blocks
	town.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	return town


## Take the sun and the horizon colour from a ProceduralSky node, so land and sky agree.
static func _match_sky(material: ShaderMaterial, sky_node: Node) -> void:
	if sky_node == null:
		return
	var sun := sky_node.get_node_or_null("Sun") as DirectionalLight3D
	if sun != null:
		material.set_shader_parameter(&"sun_direction", sun.global_transform.basis.z if sun.is_inside_tree() else sun.transform.basis.z)
		material.set_shader_parameter(&"sun_color", sun.light_color)
	var world := sky_node.get_node_or_null("Environment") as WorldEnvironment
	if world != null and world.environment != null and world.environment.sky != null:
		var sky_material := world.environment.sky.sky_material as ShaderMaterial
		var horizon = sky_material.get_shader_parameter(&"horizon_color") if sky_material != null else null
		if horizon != null:
			material.set_shader_parameter(&"haze_color", horizon)
