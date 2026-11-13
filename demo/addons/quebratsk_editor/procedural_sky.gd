extends RefCounted

## A computed sky for an imported level: gradient, sun, and clouds with real thickness (lit
## tops, grey undersides, haze towards the horizon) that drift and change with the wind.
##
## A GoldSrc level's own sky is six pictures that belong to the game it came from. They are
## right for looking at the level as it was, and wrong for a game of your own. This puts a
## sky made of arithmetic in their place, so nothing of anybody else's is carried along.
##
##   const ProceduralSky := preload("res://addons/quebratsk_editor/procedural_sky.gd")
##   var map := importer.load_map(uri)
##   ProceduralSky.attach(map, importer.load_map_entities(uri))
##
## What it adds, as one "ProceduralSky" node under the map:
##
##   - "Environment", a WorldEnvironment whose sky is procedural_sky.gdshader. Wind, cloud
##     cover and colours are parameters of its material and can be changed or animated.
##   - "Sun", a DirectionalLight3D. The sky draws its sun wherever this points from, so
##     turning the node moves the sun.
##
## One thing it cannot do: a GoldSrc level's light and shadows were computed when the level
## was compiled and are painted onto its walls. Moving the sun does not move them. So the sun
## starts where the level's own `light_environment` says it was when those shadows were
## made, and anything of yours standing in the level (which Godot does light) is lit from the
## same side as the shadows around it.

const SHADER := preload("res://addons/quebratsk_editor/procedural_sky.gdshader")

## Where the sun goes when the level does not say: fairly high, a little to one side.
const DEFAULT_SUN := Vector3(0.35, 0.8, 0.25)
## A sun lower than this (in degrees) is lifted to it: a level lit from below the horizon is
## a mapper's trick, not a sky anyone could look at.
const LOWEST_SUN_DEGREES := 4.0


## Give `map` a computed sky. `entities` is what UnifiedAssetImporter.load_map_entities()
## returned for the same level; pass an empty array to get the default sun.
## Returns the node that was added. The level's own painted sky box, if it has one, is
## hidden rather than removed, so it can be shown again.
static func attach(map: Node3D, entities: Array = []) -> Node3D:
	var painted := map.get_node_or_null("Sky") as Node3D
	if painted != null:
		painted.visible = false

	var root := Node3D.new()
	root.name = "ProceduralSky"

	var sun_facts := sun_of(entities)
	var sun := DirectionalLight3D.new()
	sun.name = "Sun"
	sun.light_color = sun_facts["color"]
	sun.shadow_enabled = true
	var toward: Vector3 = sun_facts["direction"]
	# A light shines along its own -Z, so it looks away from where the sun is.
	var up := Vector3.UP if absf(toward.y) < 0.99 else Vector3.RIGHT
	sun.transform = Transform3D(Basis.looking_at(-toward, up), Vector3.ZERO)
	root.add_child(sun)

	var noise := FastNoiseLite.new()
	noise.noise_type = FastNoiseLite.TYPE_SIMPLEX_SMOOTH
	noise.fractal_type = FastNoiseLite.FRACTAL_FBM
	noise.fractal_octaves = 5
	noise.frequency = 0.004
	var picture := NoiseTexture2D.new()
	picture.width = 512
	picture.height = 512
	picture.seamless = true
	picture.generate_mipmaps = true
	picture.noise = noise

	var material := ShaderMaterial.new()
	material.shader = SHADER
	material.set_shader_parameter(&"cloud_noise", picture)
	material.set_shader_parameter(&"shape_noise", _volume(false))
	material.set_shader_parameter(&"billow_noise", _volume(true))

	var sky := Sky.new()
	sky.sky_material = material
	# The clouds move, so what the sky lights the scene with has to be kept up to date.
	sky.process_mode = Sky.PROCESS_MODE_REALTIME
	sky.radiance_size = Sky.RADIANCE_SIZE_256

	var environment := Environment.new()
	environment.background_mode = Environment.BG_SKY
	environment.sky = sky
	environment.ambient_light_source = Environment.AMBIENT_SOURCE_SKY

	var world := WorldEnvironment.new()
	world.name = "Environment"
	world.environment = environment
	root.add_child(world)

	map.add_child(root)
	return root


## The two blocks of noise a cloud is carved from, made once and shared by every sky.
static var _volumes := {}


## A block of noise that repeats without a seam in all three directions: soft lumps for the
## body of a cloud, or round billows for its surface.
static func _volume(billows: bool) -> ImageTexture3D:
	if _volumes.has(billows):
		return _volumes[billows]
	var noise := FastNoiseLite.new()
	noise.seed = 7 if billows else 3
	noise.fractal_type = FastNoiseLite.FRACTAL_FBM
	if billows:
		noise.noise_type = FastNoiseLite.TYPE_CELLULAR
		noise.cellular_return_type = FastNoiseLite.RETURN_DISTANCE
		noise.fractal_octaves = 3
		noise.frequency = 0.07
	else:
		noise.noise_type = FastNoiseLite.TYPE_PERLIN
		noise.fractal_octaves = 4
		noise.frequency = 0.035
	const SIZE := 64
	# Cells are dark in the middle and bright at the walls; billows are the other way up.
	var slices := noise.get_seamless_image_3d(SIZE, SIZE, SIZE, billows, 0.2)
	var volume := ImageTexture3D.new()
	volume.create(slices[0].get_format(), SIZE, SIZE, SIZE, false, slices)
	_volumes[billows] = volume
	return volume


## The sky's material, for changing the wind or the clouds:
##   ProceduralSky.material_of(node).set_shader_parameter("cloud_cover", 0.8)
static func material_of(sky_node: Node) -> ShaderMaterial:
	var world := sky_node.get_node_or_null("Environment") as WorldEnvironment
	if world == null or world.environment == null or world.environment.sky == null:
		return null
	return world.environment.sky.sky_material as ShaderMaterial


## Where a level's sun is and what colour it is, read from its `light_environment`:
## {"direction": towards the sun in Godot's axes, "color": Color, "found": bool}.
##
## The compiler aimed the sunlight along `angles` (pitch yaw roll, of which only yaw counts)
## tilted by `pitch`, where -90 is straight down; the sun sits the opposite way. `_light`
## is its colour as three numbers out of 255 and a strength, which is not used here.
static func sun_of(entities: Array) -> Dictionary:
	var out := {"direction": DEFAULT_SUN.normalized(), "color": Color(1.0, 0.96, 0.88),
		"found": false}
	for entry in entities:
		var entity: Dictionary = entry
		if str(entity.get("classname", "")) != "light_environment":
			continue
		var angles := str(entity.get("angles", "")).split_floats(" ", false)
		var yaw := 0.0
		var pitch := 0.0
		if angles.size() >= 2:
			pitch = angles[0]
			yaw = angles[1]
		if entity.has("angle") and float(str(entity["angle"])) != 0.0:
			yaw = float(str(entity["angle"]))
		if entity.has("pitch") and float(str(entity["pitch"])) != 0.0:
			pitch = float(str(entity["pitch"]))

		var y := deg_to_rad(yaw)
		var p := deg_to_rad(pitch)
		# The way the light travels, in the level's own axes; the sun is where it came from.
		var travel := Vector3(cos(y) * cos(p), sin(y) * cos(p), sin(p))
		var sun := -travel
		# The level's (x, y, z) is Godot's (-y, z, -x), as for everything else imported.
		var toward := Vector3(-sun.y, sun.z, -sun.x)
		var lowest := sin(deg_to_rad(LOWEST_SUN_DEGREES))
		if toward.y < lowest:
			var flat := Vector2(toward.x, toward.z)
			flat = flat.normalized() if flat.length() > 0.001 else Vector2(1, 0)
			var reach := cos(deg_to_rad(LOWEST_SUN_DEGREES))
			toward = Vector3(flat.x * reach, lowest, flat.y * reach)
		out["direction"] = toward.normalized()

		var light := str(entity.get("_light", "")).split_floats(" ", false)
		if light.size() >= 3:
			var colour := Color(light[0] / 255.0, light[1] / 255.0, light[2] / 255.0)
			# Only its hue: a mapper's dim sun is still a bright thing in the sky.
			var peak := maxf(colour.r, maxf(colour.g, colour.b))
			if peak > 0.0:
				out["color"] = Color(colour.r / peak, colour.g / peak, colour.b / peak)
		out["found"] = true
		break
	return out
