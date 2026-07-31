extends Node3D

## Visual verification scene.
##
## Loads real retail assets through the C++ importer and puts them in front of a camera,
## so the things unit tests cannot check — winding, UV orientation, texture binding,
## skeleton hookup — become visible.
##
## Everything is optional: if an install is missing the scene still runs and says what
## it could not find, rather than failing silently.

const CS_ROOT := "C:/Program Files (x86)/Steam/steamapps/common/Half-Life/cstrike"
## Counter-Strike maps reuse the base Half-Life texture set (names like "c1a3wall05" are
## Half-Life chapter 1 area 3), which lives in valve/halflife.wad. Mounting only the
## cstrike WADs leaves most of a map untextured.
const HL_ROOT := "C:/Program Files (x86)/Steam/steamapps/common/Half-Life/valve"
const DAYZ_ADDONS := "C:/Program Files (x86)/Steam/steamapps/common/DayZ/Addons"

const MAP := "vfs://maps/cs_assault.bsp"
const CHARACTER := "vfs://cs/player/urban/urban.mdl"

var vfs: VFSManager
var importer: UnifiedAssetImporter


func _ready() -> void:
	print("=== QUEBRATSK VISUAL VERIFICATION ===")

	vfs = VFSManager.new()
	importer = UnifiedAssetImporter.new()
	importer.set_vfs(vfs)

	_setup_view()

	if DirAccess.dir_exists_absolute(CS_ROOT):
		_mount_goldsrc()
		var map_centre := _load_map()
		_load_character(map_centre)
	else:
		push_error("Counter-Strike 1.6 not found at: %s" % CS_ROOT)

	_probe_dayz()

	print("=== DONE ===")


func _setup_view() -> void:
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0.10, 0.11, 0.14)
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color(0.75, 0.75, 0.8)
	env.ambient_light_energy = 1.1

	var world := WorldEnvironment.new()
	world.environment = env
	add_child(world)

	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-50, -40, 0)
	sun.light_energy = 1.2
	add_child(sun)

	var cam := Camera3D.new()
	cam.name = "Cam"
	cam.far = 4000.0
	add_child(cam)
	cam.make_current()


func _mount_goldsrc() -> void:
	# Loose files on disk — the case container archives alone could not cover.
	var n_models := vfs.mount_directory("cs", CS_ROOT + "/models")
	var n_maps := vfs.mount_directory("maps", CS_ROOT + "/maps")
	print("mounted %d model files, %d map files" % [n_models, n_maps])

	# Map textures live in the WADs when they are not embedded in the BSP.
	var wads := 0
	for root in [HL_ROOT, CS_ROOT]:
		var dir := DirAccess.open(root)
		if dir == null:
			continue
		for f in dir.get_files():
			if f.get_extension().to_lower() == "wad":
				if vfs.mount_container("wad_" + f.get_basename(), root + "/" + f):
					wads += 1
	print("mounted %d WAD archives" % wads)


func _load_map() -> Vector3:
	if not vfs.file_exists(MAP):
		push_error("map not in VFS: %s" % MAP)
		return Vector3.ZERO

	var mesh: ArrayMesh = importer.load_mesh(MAP)
	if mesh == null or mesh.get_surface_count() == 0:
		push_error("load_mesh produced nothing for %s" % MAP)
		return Vector3.ZERO

	var mi := MeshInstance3D.new()
	mi.name = "Map"
	mi.mesh = mesh
	add_child(mi)

	var textured := 0
	for i in mesh.get_surface_count():
		var mat := mesh.surface_get_material(i)
		if mat is StandardMaterial3D and mat.albedo_texture != null:
			textured += 1

	var ab := mesh.get_aabb()
	print("map -> %d surfaces, %d textured, aabb %s" % [mesh.get_surface_count(), textured, str(ab)])
	return ab.get_center()


func _load_character(map_centre: Vector3) -> void:
	if not vfs.file_exists(CHARACTER):
		push_error("character not in VFS: %s" % CHARACTER)
		return

	var node := importer.load_model(CHARACTER)
	if node == null:
		push_error("load_model returned null for %s" % CHARACTER)
		return

	add_child(node)
	node.position = map_centre

	var mi: MeshInstance3D = null
	for c in node.get_children():
		if c is MeshInstance3D:
			mi = c
	if mi == null:
		push_error("no MeshInstance3D under the loaded model")
		return

	# Drop the character so its feet sit at the map centre height rather than its origin.
	var ab := mi.get_aabb()
	node.position.y = map_centre.y - ab.position.y

	var skel_bones := 0
	if node is Skeleton3D:
		skel_bones = node.get_bone_count()

	print("character -> %s, bones=%d, surfaces=%d, skin=%s" % [
		node.name, skel_bones, mi.mesh.get_surface_count(),
		"yes" if mi.skin != null else "no"])

	# Frame the character, with the map behind it.
	var cam: Camera3D = $Cam
	var focus := node.position + Vector3(0, 0.9, 0)
	cam.position = focus + Vector3(2.6, 0.6, 2.6)
	cam.look_at(focus, Vector3.UP)


func _probe_dayz() -> void:
	## DayZ ships Real Virtuality .pbo archives. Mounting and indexing them works; the
	## models inside are .p3d, for which the parser is still a stub, so nothing can be
	## turned into geometry yet. Report exactly how far it gets.
	if not DirAccess.dir_exists_absolute(DAYZ_ADDONS):
		print("dayz -> not installed, skipping")
		return

	var probe := "characters_belts.pbo"
	if not FileAccess.file_exists(DAYZ_ADDONS + "/" + probe):
		print("dayz -> %s not found, skipping" % probe)
		return

	if not vfs.mount_container("dayz", DAYZ_ADDONS + "/" + probe):
		push_error("dayz -> failed to mount %s" % probe)
		return

	var all := vfs.list_files("vfs://dayz/")
	var p3d := 0
	var paa := 0
	for f in all:
		var e := f.get_extension().to_lower()
		if e == "p3d":
			p3d += 1
		elif e == "paa":
			paa += 1

	print("dayz -> mounted %s: %d entries indexed (%d .p3d models, %d .paa textures)"
		% [probe, all.size(), p3d, paa])
	print("dayz -> geometry NOT importable: P3DMLODParser is a stub, PAADecoder unimplemented")
