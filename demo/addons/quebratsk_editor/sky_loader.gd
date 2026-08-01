@tool
extends RefCounted
class_name QuebratskSkyLoader

## Building a Godot sky out of a GoldSrc map's own skybox.
##
## A map names its sky in worldspawn, as `skyname`, and the pictures sit beside the game in
## gfx/env as six separate images: <name>up, <name>dn, <name>lf, <name>rt, <name>ft, <name>bk.
## Without them a level renders against flat grey, which is what every import here looked
## like: the geometry was right and the world it sat in was missing.
##
## Lives on its own rather than in the C++ importer for one reason: these are almost always
## Targa files, and Godot already decodes Targa. Writing a TGA decoder in the engine to do
## in C++ what Image.load_tga_from_buffer does in one call would be work spent to end up in
## the same place.

## The order Godot's Cubemap expects its layers in, and the GoldSrc face that goes in each.
##
## GoldSrc names its faces by where you are looking, and a cubemap indexes them by axis, so
## right is +X and front is -Z. The two that are not obvious are up and down: a Quake-era
## skybox stores those rotated, because the tools that made them drew them as if you were
## looking at a map from above.
const FACES := [
	{"suffix": "rt", "rotate": 0},    # +X
	{"suffix": "lf", "rotate": 0},    # -X
	{"suffix": "up", "rotate": 90},   # +Y
	{"suffix": "dn", "rotate": -90},  # -Y
	{"suffix": "ft", "rotate": 0},    # +Z
	{"suffix": "bk", "rotate": 0},    # -Z
]

const SKY_SHADER := """
shader_type sky;

uniform samplerCube faces : source_color, filter_linear;

void sky() {
	// EYEDIR is the direction this pixel of the sky is being looked at, which is exactly
	// what a cubemap is indexed by, so there is no projection to undo.
	COLOR = texture(faces, EYEDIR).rgb;
}
"""


## The name a map gives its sky, or an empty string.
static func sky_name(entities: Array) -> String:
	for e in entities:
		var entity: Dictionary = e
		if str(entity.get("classname", "")) == "worldspawn":
			return str(entity.get("skyname", ""))
	return ""


## Build the sky a map asks for, or null when its images are not on this machine.
##
## `report` receives one line per face that could not be found, so a missing skybox can say
## which file it wanted rather than silently leaving the world grey.
static func load_sky(vfs: VFSManager, entities: Array, report: Array = []) -> Sky:
	var name := sky_name(entities)
	if name.is_empty():
		return null

	var images: Array[Image] = []
	for face in FACES:
		var image := _load_face(vfs, name, str(face["suffix"]))
		if image == null:
			report.append("gfx/env/%s%s" % [name, face["suffix"]])
			return null
		if int(face["rotate"]) != 0:
			image.rotate_90(CLOCKWISE if int(face["rotate"]) > 0 else COUNTERCLOCKWISE)
		images.append(image)

	# Every face of a cubemap has to be the same size, and a hand-made skybox is not always
	# consistent about that.
	var side: int = images[0].get_width()
	for image in images:
		if image.get_width() != side or image.get_height() != side:
			image.resize(side, side, Image.INTERPOLATE_BILINEAR)

	var cube := Cubemap.new()
	cube.create_from_images(images)

	var shader := Shader.new()
	shader.code = SKY_SHADER
	var material := ShaderMaterial.new()
	material.shader = shader
	material.set_shader_parameter("faces", cube)

	var sky := Sky.new()
	sky.sky_material = material
	return sky


static func _load_face(vfs: VFSManager, name: String, suffix: String) -> Image:
	# Targa first because that is what these are, then the others a repack might have left.
	for ext in [".tga", ".bmp", ".png"]:
		var hit: Dictionary = vfs.find_files("gfx/env/%s%s%s" % [name, suffix, ext],
			PackedStringArray(), PackedStringArray(), PackedStringArray(), 1)
		var files: PackedStringArray = hit["files"]
		if files.is_empty():
			continue

		var bytes := vfs.read_file(str(files[0]))
		if bytes.is_empty():
			continue

		var image := Image.new()
		var err := ERR_FILE_UNRECOGNIZED
		match ext:
			".tga": err = image.load_tga_from_buffer(bytes)
			".bmp": err = image.load_bmp_from_buffer(bytes)
			".png": err = image.load_png_from_buffer(bytes)
		if err == OK and image.get_width() > 0:
			return image
	return null
