extends Node3D

## Cross-engine import demo: a GoldSrc map populated with Source-engine character
## models taken straight from Garry's Mod Workshop archives.
##
## Nothing is extracted by hand. The .gma addons are mounted as virtual filesystems and
## every asset — model, skeleton, textures — is read out of them at runtime.

const CS_ROOT := "C:/Program Files (x86)/Steam/steamapps/common/Half-Life/cstrike"
const HL_ROOT := "C:/Program Files (x86)/Steam/steamapps/common/Half-Life/valve"
const WORKSHOP := "C:/Program Files (x86)/Steam/steamapps/workshop/content/4000"

const MAP := "vfs://maps/cs_assault.bsp"

## Workshop id -> the player model inside that addon.
const SOLDIERS := {
	"3582369292": "models/player/lowpoly/usarmy_2000.mdl",     # US Army
	"3589860237": "models/player/lowpoly/britarmy_2000.mdl",   # British Army
	"3593915830": "models/player/lowpoly/gerarmy_2000.mdl",    # German Army
	"3584305751": "models/player/lowpoly/ruarmy_1995.mdl",     # Russian Army 90s
	"3664555698": "models/player/lowpoly/basarmy_2007.mdl",    # Basetian Army
	"3735175058": "models/player/lowpoly/fplf_2002.mdl",       # FPLF Fighters
	"3579662934": "models/player/lowpoly/4ervo/lp_soldier.mdl", # Special Forces
}

var vfs: VFSManager
var importer: UnifiedAssetImporter


func _ready() -> void:
	print("=== QUEBRATSK CROSS-ENGINE DEMO ===")
	print("GoldSrc map + Source-engine models from Garry's Mod Workshop\n")

	vfs = VFSManager.new()
	importer = UnifiedAssetImporter.new()
	importer.set_vfs(vfs)

	_setup_view()
	_mount_goldsrc()
	var mounted := _mount_workshop()
	var ground := _load_map()
	_spawn_soldiers(mounted, ground)

	print("\n=== DONE ===")


func _setup_view() -> void:
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0.53, 0.66, 0.83)
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color(0.8, 0.8, 0.85)
	env.ambient_light_energy = 1.2
	var world := WorldEnvironment.new()
	world.environment = env
	add_child(world)

	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-48, -38, 0)
	sun.light_energy = 1.3
	add_child(sun)

	var cam := Camera3D.new()
	cam.name = "Cam"
	cam.far = 4000.0
	add_child(cam)
	cam.make_current()


func _mount_goldsrc() -> void:
	vfs.mount_directory("maps", CS_ROOT + "/maps")
	var wads := 0
	for root in [HL_ROOT, CS_ROOT]:
		var dir := DirAccess.open(root)
		if dir == null:
			continue
		for f in dir.get_files():
			if f.get_extension().to_lower() == "wad":
				if vfs.mount_container("wad_" + f.get_basename(), root + "/" + f):
					wads += 1
	print("GoldSrc: %d WAD archives mounted" % wads)


## Mount each subscribed addon as its own VFS namespace. Returns the ids that mounted.
func _mount_workshop() -> Array:
	var mounted := []
	for key in SOLDIERS:
		var id := str(key)
		var addon_dir: String = WORKSHOP + "/" + id
		var dir := DirAccess.open(addon_dir)
		if dir == null:
			push_warning("workshop addon not downloaded: %s" % id)
			continue
		for f in dir.get_files():
			if f.get_extension().to_lower() == "gma":
				if vfs.mount_container(id, addon_dir + "/" + f):
					mounted.append(id)
				break
	print("Workshop: %d/%d .gma addons mounted" % [mounted.size(), SOLDIERS.size()])
	return mounted


func _load_map() -> float:
	if not vfs.file_exists(MAP):
		push_error("map not in VFS: %s" % MAP)
		return 0.0

	var mesh: ArrayMesh = importer.load_mesh(MAP)
	if mesh == null or mesh.get_surface_count() == 0:
		push_error("map produced no geometry")
		return 0.0

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
	print("Map: %d surfaces, %d textured" % [mesh.get_surface_count(), textured])
	return ab.position.y


func _spawn_soldiers(mounted: Array, ground_y: float) -> void:
	print("\nSoldiers:")
	var placed := 0
	var spacing := 1.6

	for key in mounted:
		var id := str(key)
		var uri: String = "vfs://%s/%s" % [id, SOLDIERS[id]]
		if not vfs.file_exists(uri):
			push_error("  %s -> not found in VFS" % uri)
			continue

		var node := importer.load_model(uri)
		if node == null:
			push_error("  %s -> load_model returned null" % uri)
			continue

		add_child(node)

		# Line them up shoulder to shoulder, feet on the same plane.
		var mi: MeshInstance3D = null
		for c in node.get_children():
			if c is MeshInstance3D:
				mi = c
		var foot := 0.0
		if mi != null:
			foot = mi.get_aabb().position.y
		node.position = Vector3((placed - 3) * spacing, ground_y - foot + 0.9, 0)
		node.rotation_degrees = Vector3(0, 180, 0)

		var bones := 0
		if node is Skeleton3D:
			bones = node.get_bone_count()
		var surfaces := 0
		var textured := 0
		if mi != null and mi.mesh != null:
			surfaces = mi.mesh.get_surface_count()
			for i in surfaces:
				var mat := mi.mesh.surface_get_material(i)
				if mat is StandardMaterial3D and mat.albedo_texture != null:
					textured += 1

		var verts := 0
		var ab := AABB()
		if mi != null and mi.mesh != null:
			ab = mi.get_aabb()
			for i in surfaces:
				verts += mi.mesh.surface_get_arrays(i)[Mesh.ARRAY_VERTEX].size()

		print("  %-26s bones=%-3d surf=%d tex=%d verts=%-6d size=(%.2f, %.2f, %.2f) skin=%s"
			% [SOLDIERS[id].get_file(), bones, surfaces, textured, verts,
			   ab.size.x, ab.size.y, ab.size.z,
			   "yes" if (mi != null and mi.skin != null) else "no"])
		placed += 1

	print("Placed %d soldiers" % placed)

	# Frame the line-up.
	var cam: Camera3D = $Cam
	var focus := Vector3(-0.8, ground_y + 1.7, 0)
	cam.position = focus + Vector3(0, 0.4, 7.5)
	cam.look_at(focus, Vector3.UP)
