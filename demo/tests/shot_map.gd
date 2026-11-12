extends Node3D

## Render one map from a player spawn and save what the camera sees as a PNG.
##
## The other suites count things; none of them looks. This is the tool for looking: it
## loads a map exactly as the plugin does, stands where a player would spawn, and writes
## the frame to disk so it can be inspected or compared against a later run.
##
##   godot --path demo res://tests/shot_map.tscn -- bshift/maps/ba_tram1.bsp out.png [yaw_deg] [pitch_deg] [sky]
##
## `sky` is `procedural` to see the level under the computed sky instead of its own, and
## `procedural:<seconds>` to see that sky as it is after that long, which is how two frames
## show whether the clouds move. More words may follow it, separated by commas:
##   `sun`      look straight at the sun instead of where the spawn faces
##   `unbaked`  take the level's painted light off and let the sun and lamps light it
##   `high`     from far above the level, so that only the sky is in the picture
## `loader` instead shows the level's own sky as the older sky_loader.gd builds it, to
## compare against the box the importer makes.
##
## The first argument is matched against the map's path, so "ba_tram1" is enough. It must
## run with a window (no --headless): the headless renderer draws nothing.

const ProceduralSky := preload("res://addons/quebratsk_editor/procedural_sky.gd")
const OwnLighting := preload("res://addons/quebratsk_editor/own_lighting.gd")
const SkyLoader := preload("res://addons/quebratsk_editor/sky_loader.gd")

const UNITS_TO_METERS := 0.0254


func _ready() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() < 2:
		print("usage: -- <map path fragment> <out.png> [extra yaw in degrees] [pitch in degrees]")
		get_tree().quit(2)
		return
	var needle := str(args[0])
	var out_path := str(args[1])
	var extra_yaw := float(args[2]) if args.size() > 2 else 0.0
	var pitch := float(args[3]) if args.size() > 3 else 0.0
	var sky_mode := str(args[4]) if args.size() > 4 else ""

	var vfs := VFSManager.new()
	var importer := UnifiedAssetImporter.new()
	add_child(vfs)
	add_child(importer)
	importer.set_vfs(vfs)

	var games: Dictionary = SteamLibraryDetector.detect_installed_games()
	var n := 0
	for title in games:
		var root := str(games[title])
		var scan: Dictionary = vfs.scan_game_directory(root)
		for archive in scan.get("archives", []):
			if vfs.mount_container("g%d" % n, str(archive)):
				n += 1
		vfs.mount_directory("loose%d" % n, root)
		n += 1

	var hit: Dictionary = vfs.find_files(needle, PackedStringArray(["bsp"]),
		PackedStringArray(), PackedStringArray(), 0)
	var files: Array = hit.get("files", [])
	if files.is_empty():
		print("no map matches '%s'  ** FAILED **" % needle)
		get_tree().quit(1)
		return
	var uri := str(files[0])
	print("map: %s" % uri)

	var map: Node3D = importer.load_map(uri)
	if map == null:
		print("map would not load, error %d  ** FAILED **" % importer.get_last_error_code())
		get_tree().quit(1)
		return
	add_child(map)

	var entities: Array = importer.load_map_entities(uri)
	print("entities: %d" % entities.size())

	var eye := Vector3.ZERO
	var yaw := 0.0
	var found_spawn := false
	for e in entities:
		var ent: Dictionary = e
		var cls := str(ent.get("classname", ""))
		if cls.begins_with("info_player") and ent.has("origin"):
			var parts := str(ent["origin"]).split_floats(" ", false)
			if parts.size() < 3:
				continue
			# Valve (x, y, z) -> Godot (-y, z, -x), inches to metres. Eyes sit 28 units
			# above the hull centre.
			eye = Vector3(-parts[1], parts[2] + 28.0, -parts[0]) * UNITS_TO_METERS
			# Facing is either "angles" (pitch yaw roll) or the older single "angle" (yaw).
			var angles := str(ent.get("angles", "")).split_floats(" ", false)
			if angles.size() >= 2:
				yaw = angles[1]
			elif ent.has("angle"):
				yaw = float(str(ent["angle"]))
			found_spawn = true
			print("spawn: %s at %s yaw %.0f" % [cls, str(eye.snapped(Vector3.ONE * 0.01)), yaw])
			break
	if not found_spawn:
		print("no player spawn readable from the entities; using the map's centre")
		var mesh_instance := map as MeshInstance3D
		if mesh_instance and mesh_instance.mesh:
			eye = mesh_instance.mesh.get_aabb().get_center()

	var camera := Camera3D.new()
	camera.fov = 90.0
	camera.near = 0.05
	camera.far = 500.0
	add_child(camera)
	camera.position = eye
	camera.rotation = Vector3(deg_to_rad(pitch), deg_to_rad(yaw + extra_yaw), 0.0)
	camera.make_current()

	var words := sky_mode.split(",", false)
	if words.has("high"):
		# Far above the level, where nothing but the sky is left to see.
		camera.position.y += 5000.0
	if sky_mode.begins_with("loader"):
		var old_sky := SkyLoader.load_sky(vfs, entities)
		var painted := map.get_node_or_null("Sky") as Node3D
		if old_sky == null or painted == null:
			print("no sky to compare  ** FAILED **")
			get_tree().quit(1)
			return
		painted.visible = false
		var old_env := Environment.new()
		old_env.background_mode = Environment.BG_SKY
		old_env.sky = old_sky
		old_env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
		old_env.ambient_light_color = Color.WHITE
		var old_world := WorldEnvironment.new()
		old_world.environment = old_env
		add_child(old_world)
	elif sky_mode.begins_with("procedural"):
		var sky_node := ProceduralSky.attach(map, entities)
		var facts := ProceduralSky.sun_of(entities)
		if words.has("sun"):
			camera.look_at(eye + facts["direction"], Vector3.FORWARD)
		if words.has("unbaked"):
			var surfaces := OwnLighting.unpaint(map)
			var lamps := OwnLighting.add_lamps(map, entities)
			print("unbaked: %d surfaces, %d lamps" % [surfaces, lamps.get_child_count()])
		print("procedural sky: sun towards %s (%s)" % [str(facts["direction"].snapped(Vector3.ONE * 0.01)),
			"from the level" if facts["found"] else "default"])
		# The noise picture is drawn on another thread; a frame taken before it is ready
		# shows a sky with no clouds in it.
		var picture: NoiseTexture2D = ProceduralSky.material_of(sky_node).get_shader_parameter(&"cloud_noise")
		await picture.changed
		var wait := float(words[0].get_slice(":", 1)) if words[0].contains(":") else 0.0
		if wait > 0.0:
			await get_tree().create_timer(wait).timeout
	else:
		var env := Environment.new()
		env.background_mode = Environment.BG_COLOR
		env.background_color = Color(0.35, 0.0, 0.35)  # magenta: any hole in the level shows
		env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
		env.ambient_light_color = Color.WHITE
		env.ambient_light_energy = 1.0
		var world_env := WorldEnvironment.new()
		world_env.environment = env
		add_child(world_env)

	for i in 6:
		await get_tree().process_frame
	await RenderingServer.frame_post_draw

	var image := get_viewport().get_texture().get_image()
	var err := image.save_png(out_path)
	if err != OK:
		print("could not write %s (error %d)  ** FAILED **" % [out_path, err])
		get_tree().quit(1)
		return
	print("saved %s (%dx%d)" % [out_path, image.get_width(), image.get_height()])
	get_tree().quit(0)
