@tool
extends RefCounted

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
## Derived rather than chosen. GoldSrc names its faces by where you are standing and looking,
## in Valve's Z-up frame: ft is +X, bk is -X, lf is +Y, rt is -Y, up is +Z, dn is -Z. Godot
## indexes a cubemap by axis in the order +X, -X, +Y, -Y, +Z, -Z. Running each Valve axis
## through the same remap the geometry uses, source_to_godot(v) = (-v.y, v.z, -v.x):
##
##     Valve +X (ft) -> Godot (0, 0, -1)   = -Z
##     Valve -X (bk) -> Godot (0, 0,  1)   = +Z
##     Valve +Y (lf) -> Godot (-1, 0, 0)   = -X
##     Valve -Y (rt) -> Godot ( 1, 0, 0)   = +X
##     Valve +Z (up) -> Godot ( 0, 1, 0)   = +Y
##     Valve -Z (dn) -> Godot ( 0,-1, 0)   = -Y
##
## Front and back were the wrong way round here, which put a hard diagonal seam across the
## sky of every level: two faces meeting that were never meant to touch. It reads as a
## texture problem and is a bookkeeping one.
const FACES := [
	{"suffix": "rt", "rotate": 0},    # +X
	{"suffix": "lf", "rotate": 0},    # -X
	{"suffix": "up", "rotate": 90},   # +Y
	{"suffix": "dn", "rotate": -90},  # -Y
	{"suffix": "bk", "rotate": 0},    # +Z
	{"suffix": "ft", "rotate": 0},    # -Z
]

## Where a replacement skybox lives, by map name: res://assets/skyboxes/<map>/<map>_up.png
## and its five companions.
##
## The game's own skies are 1999 Targas at 256 pixels a side, which is what they should be for
## anyone restoring a level. Somebody building a game on top of one wants better, and this is
## where they put it without touching the install.
const OVERRIDE_FOLDER := "res://assets/skyboxes"


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
static func load_sky(vfs: VFSManager, entities: Array, report: Array = [],
		override := "") -> Sky:
	var name := sky_name(entities)
	if name.is_empty() and override.is_empty():
		return null

	# One equirectangular image, if somebody supplied one, before six faces.
	#
	# Not because a cube shows its corners — it does not; a cubemap is indexed by direction
	# and there is no projection to give itself away. The corners people see are two faces
	# that do not continue into each other, and a sphere has exactly the same problem with a
	# seam down one side and a pinch at each pole.
	#
	# The real difference is in making one. A panorama is a single picture with two edges to
	# reconcile instead of six faces with twelve, so a good one is far easier to come by.
	var panorama := _load_panorama(override)
	if panorama != null:
		var panorama_material := PanoramaSkyMaterial.new()
		panorama_material.panorama = panorama
		panorama_material.filter = true
		var panorama_sky := Sky.new()
		panorama_sky.sky_material = panorama_material
		return panorama_sky

	var images: Array[Image] = []
	for face in FACES:
		var image := _load_override(override, str(face["suffix"]))
		if image == null:
			image = _load_face(vfs, name, str(face["suffix"]))
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


## An equirectangular panorama for this map, if one was put there.
##
## res://assets/skyboxes/<map>/<map>_panorama.png. Twice as wide as it is tall: longitude runs
## the full width and latitude the full height, so the top row is straight up and the bottom
## row straight down.
static func _load_panorama(map: String) -> Texture2D:
	if map.is_empty():
		return null
	# Largest first. A panorama tends to arrive in a couple of sizes and the big one is the
	# reason to have made it; the smaller is there for a machine that cannot hold it.
	for size in ["_8k", "_4k", ""]:
		for ext in [".png", ".jpg"]:
			var path := "%s/%s/%s_panorama%s%s" % [OVERRIDE_FOLDER, map, map, size, ext]
			if not FileAccess.file_exists(path):
				continue
			var bytes := FileAccess.get_file_as_bytes(path)
			var image := Image.new()
			var err := image.load_png_from_buffer(bytes) if ext == ".png" 				else image.load_jpg_from_buffer(bytes)
			if err == OK and image.get_width() > 0:
				print("[sky] panorama %s (%dx%d)"
					% [path.get_file(), image.get_width(), image.get_height()])
				return ImageTexture.create_from_image(image)
	return null


## A replacement face from the project, if one was put there for this map.
##
## Named by the map rather than by the sky, because two maps can share a sky name and want
## different weather: de_aztec asks for a sky called "doom1", which it shares with nothing.
static func _load_override(map: String, suffix: String) -> Image:
	if map.is_empty():
		return null
	# Decoded from the file's own bytes rather than through load().
	#
	# A .png dropped into the project has no .import beside it until the editor has run, so
	# load() returns nothing and the game quietly falls back to the game's own sky. That is
	# exactly what happened here: six new faces sat on disk unused while the level rendered
	# the 1999 ones, and the only way to tell was that the storm clouds never appeared.
	for ext in [".png", ".tga", ".jpg"]:
		var path := "%s/%s/%s_%s%s" % [OVERRIDE_FOLDER, map, map, suffix, ext]
		if not FileAccess.file_exists(path):
			continue
		var bytes := FileAccess.get_file_as_bytes(path)
		if bytes.is_empty():
			continue
		var image := Image.new()
		var err := ERR_FILE_UNRECOGNIZED
		match ext:
			".png": err = image.load_png_from_buffer(bytes)
			".tga": err = image.load_tga_from_buffer(bytes)
			".jpg": err = image.load_jpg_from_buffer(bytes)
		if err == OK and image.get_width() > 0:
			return image
	return null


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
