extends Node3D

## A playable level built out of a game you already own, for finding out what is still wrong.
##
## Press play. It mounts Counter-Strike 1.6, opens a map, and drops you into it with a weapon
## and some company. Nothing here is authored: every model, texture, animation and sound is
## read out of a retail install at startup.
##
## This replaces an earlier sandbox that had grown to thirteen hundred lines of accumulated
## patches, and it is deliberately smaller. Most of what it used to work out for itself now
## comes from res://presets — which of a model's hundred and eleven sequences is walking, what
## a weapon sounds like, how fast it lets you move — so this file only has to place things.
##
## Controls: WASD move, mouse look, click to shoot, R reload, Space jump, Ctrl crouch,
## Shift walk quietly, Escape for the cursor.

const ModelPreset := preload("res://addons/quebratsk_editor/model_preset.gd")
const EntityCatalogue := preload("res://addons/quebratsk_editor/entity_catalogue.gd")
const SoundLoader := preload("res://addons/quebratsk_editor/sound_loader.gd")
const SkyLoader := preload("res://addons/quebratsk_editor/sky_loader.gd")
const RoomAcoustics := preload("res://addons/quebratsk_editor/room_acoustics.gd")
const AcousticsScript := preload("res://lab/acoustics.gd")
const BrushesScript := preload("res://lab/brushes.gd")
const WeatherScript := preload("res://lab/weather.gd")
const PlayerScript := preload("res://lab/player.gd")
const NpcScript := preload("res://lab/npc.gd")

## The game's own content folder. Steam keeps Half-Life, Counter-Strike, Condition Zero and
## its Deleted Scenes inside one folder called Half-Life, so mounting "the game" mounts four
## of them and the result is a mixture nobody can judge.
const GAME := "cstrike"

## What the game falls back to when it cannot find something in its own folder.
##
## Counter-Strike declares this itself, in liblist.gam as `fallback_dir "valve"`, and it is not
## a detail: the door sounds, the ambient loops and a good deal else were never copied into
## cstrike because the engine finds them next door. Mounting only cstrike gave thirteen doors
## with no sound and a map asking for ambience/drips.wav that is right there in valve.
const FALLBACK := "valve"
const PRESETS := "Counter-Strike 1.6"

## Which map to open.
##
## Not de_dust2, and the reason is worth writing down: of the 25 maps Counter-Strike ships,
## exactly five declare no sound at all, and de_dust2 is one of them. Testing on it meant the
## ambient sound path was never once exercised, and its silence read as a defect for weeks.
##
## de_aztec has rain and thunder. de_torn has a hundred ambient_generic entities, de_piranesi
## ninety-six, cs_italy an opera and some chickens.
const MAP := "cs_siege"

const ENEMY_COUNT := 3

var vfs: VFSManager
var importer: UnifiedAssetImporter

var _player: CharacterBody3D
var _map_uri := ""
var _spawns: Array[Vector3] = []
var _hud: Label
var _weather: Node3D
var _thunder_zones: Array[AABB] = []
var _thunder_ready := 0.0
var _thunder_interval := 0.0
var _triggered_sounds := {}
var _water_shader: Shader
var _cleared := false

## Where the level says it sounds like somewhere: an origin, a radius and a room type. Sorted
## smallest first, so standing inside a cupboard inside a hall gives you the cupboard.
var _rooms: Array = []
var _room_now := -1
var _acoustics: Node

## Sounds built before the buses existed, waiting to be put on one. The map's ambience is
## placed while the level is being read and the listener does not exist until the player does.
var _pending_tracked: Array = []
var _storm_sky: Sky
var _built := {}
var _rng := RandomNumberGenerator.new()


func _ready() -> void:
	_rng.randomize()
	vfs = VFSManager.new()
	importer = UnifiedAssetImporter.new()
	add_child(vfs)
	add_child(importer)
	importer.set_vfs(vfs)

	_build_hud()
	_say("Mounting %s..." % GAME)
	await get_tree().process_frame

	if _mount() == 0:
		_say("Counter-Strike 1.6 is not installed, or its %s folder is missing." % GAME)
		return
	if not _open_map():
		_say("No map matching '%s' could be read." % MAP)
		return

	# A map's collider does not exist in the physics world until a physics frame has run, so
	# anything placed before this finds no floor beneath it and is left hanging in the air.
	await get_tree().physics_frame

	_place_player()

	# The buses exist before anything that makes a noise is built, so every sound can be put
	# on the right one as it is created rather than moved afterwards.
	_place_enemies()
	_refresh_hud()


# ------------------------------------------------------------------- mounting ----

func _mount() -> int:
	var mounted := 0
	for title in SteamLibraryDetector.detect_installed_games():
		var root: String = str(SteamLibraryDetector.detect_installed_games()[title]).path_join(GAME)
		if not DirAccess.dir_exists_absolute(root):
			continue
		var n := 0
		for archive in vfs.scan_game_directory(root).get("archives", []):
			if vfs.mount_container("g%d" % n, str(archive)):
				n += 1
		# The loose tree as well as the archives: GoldSrc keeps its maps, models and sounds as
		# plain files beside the game and only its textures in .wad archives.
		vfs.mount_directory("loose", root)
		mounted += n + 1

		# Behind the game's own content, so cstrike wins wherever both have a file.
		var beneath: String = str(SteamLibraryDetector.detect_installed_games()[title]) 			.path_join(FALLBACK)
		if DirAccess.dir_exists_absolute(beneath):
			for archive in vfs.scan_game_directory(beneath).get("archives", []):
				if vfs.mount_container("fb%d" % n, str(archive)):
					n += 1
					mounted += 1
			vfs.mount_directory("fallback", beneath)
			mounted += 1
		break
	if mounted > 0:
		print("[lab] mounted %s: %d container(s)" % [GAME, mounted])
	return mounted


# ---------------------------------------------------------------------- level ----

func _open_map() -> bool:
	var hit: Dictionary = vfs.find_files(MAP, PackedStringArray(["bsp"]),
		PackedStringArray(), PackedStringArray(), 1)
	var files: PackedStringArray = hit["files"]
	if files.is_empty():
		return false

	_map_uri = str(files[0])
	var map: Node3D = importer.load_map(_map_uri)
	if map == null:
		return false
	add_child(map)
	_built["map"] = _map_uri.get_file()

	var entities: Array = importer.load_map_entities(_map_uri)
	_read_spawns(entities)
	_place_ambience(entities)
	_apply_sky(entities)
	_build_weather(entities, map)

	# What each brush entity does, now that each of them is a thing that can do it.
	var brushes: Node = BrushesScript.new()
	brushes.name = "Brushes"
	add_child(brushes)
	brushes.setup(self, map, entities)
	return true


## Where the map says people begin.
##
## Counter-Strike puts counter-terrorists at info_player_start and terrorists at
## info_player_deathmatch, and uses about twenty of each.
func _read_spawns(entities: Array) -> void:
	for e in entities:
		var entity: Dictionary = e
		var cls := str(entity.get("classname", ""))
		# "position", not "origin": the importer converts the map's own origin string into
		# Godot's axes and metres and publishes it under a different name, and reading the raw
		# one back gets a String where a Vector3 is expected.
		if cls == "info_player_start" or cls == "info_player_deathmatch":
			if entity.has("position"):
				_spawns.append(entity["position"] as Vector3)
	print("[lab] %s declares %d spawn point(s)" % [_map_uri.get_file(), _spawns.size()])


## Give the level back the noise it declares.
##
## A GoldSrc map records its own ambience as ambient_generic entities: a sound name, a place,
## a volume out of ten, and spawnflags saying whether it loops and how far it carries. Every
## import until now read those and threw them away.
func _place_ambience(entities: Array) -> void:
	const PLAY_EVERYWHERE := 1   # the sound fills the level rather than coming from a point
	var placed := 0

	for e in entities:
		var entity: Dictionary = e
		if str(entity.get("classname", "")) != "ambient_generic":
			continue
		var named := str(entity.get("message", ""))
		if named.is_empty():
			continue

		var found: PackedStringArray = importer.resolve_sound(named, _map_uri)
		if found.is_empty():
			print("[lab] ambient sound not found: %s" % named)
			continue

		var flags := int(entity.get("spawnflags", 0))
		var looping := not EntityCatalogue.flag_set("ambient_generic", flags, "not toggled")
		var stream: AudioStream = SoundLoader.load_sound(vfs, str(found[0]), looping)
		if stream == null:
			continue

		var node := AudioStreamPlayer3D.new()
		node.stream = stream
		# A sound with a targetname is one somebody triggers, and on de_aztec that is the
		# thunder: it starts silent and waits. Playing everything on sight would have the
		# storm crashing continuously from the moment the level opens.
		var triggered := not str(entity.get("targetname", "")).is_empty()
		node.autoplay = not triggered
		# health is the volume, out of ten, which is a fact about the game that the map file
		# never states and the entity schema does.
		var volume := clampf(float(entity.get("health", 10)) / 10.0, 0.05, 1.0)
		node.volume_db = linear_to_db(volume)
		if (flags & PLAY_EVERYWHERE) != 0:
			node.attenuation_model = AudioStreamPlayer3D.ATTENUATION_DISABLED
		node.position = entity.get("position", Vector3.ZERO)
		add_child(node)
		_pending_tracked.append(node)
		if triggered:
			_triggered_sounds[str(entity.get("targetname", ""))] = node
		else:
			placed += 1

	_built["ambience"] = "%d sound(s)" % placed
	print("[lab] %s: %d ambient sound(s) playing, %d waiting to be triggered"
		% [_map_uri.get_file(), placed, _triggered_sounds.size()])


## The game's own skybox, six .tga faces named after what worldspawn asks for.
##
## de_aztec asks for "doom1", which sounds like a mistake and is not: that is the name of the
## sky Valve shipped with it. Reading the name and not the faces is what the first version of
## this lab did, and it left every level under Godot's default grey.
func _apply_sky(entities: Array) -> void:
	var named := SkyLoader.sky_name(entities)
	if named.is_empty():
		return
	_built["sky"] = named

	var report := []
	# The map's own name, so a replacement skybox in res://assets/skyboxes/de_aztec/ is used
	# instead of the game's 256-pixel Targas when somebody has made one.
	var report_name := _map_uri.get_file().get_basename()
	var sky: Sky = SkyLoader.load_sky(vfs, entities, report, report_name)
	if sky == null:
		_built["sky"] = "%s (faces not found)" % named
		for line in report:
			print("[lab] %s" % str(line))
		return

	# Moving cloud over the painted backdrop, and a flash that lights the whole sky from
	# inside rather than merely brightening it.
	_storm_sky = SkyLoader.make_storm_sky(report_name, sky)
	if _storm_sky != null:
		sky = _storm_sky

	var environment := Environment.new()
	environment.background_mode = Environment.BG_SKY
	environment.sky = sky
	# The level's own lightmaps already carry its lighting, so ambient light from the sky
	# would double it. The sky is there to be seen, not to light anything.
	environment.ambient_light_source = Environment.AMBIENT_SOURCE_SKY
	environment.ambient_light_sky_contribution = 0.35
	environment.ambient_light_energy = 0.6
	environment.tonemap_mode = Environment.TONE_MAPPER_FILMIC

	# Looking toward the sun should hurt a little. The panorama is an ordinary 8-bit image so
	# its sun cannot exceed white on its own; the sky shader lifts the disc above 1.0 and this
	# is what turns that into a bloom rather than a flat bright patch.
	environment.glow_enabled = true
	environment.glow_intensity = 0.9
	environment.glow_bloom = 0.15
	environment.glow_hdr_threshold = 1.0
	environment.glow_blend_mode = Environment.GLOW_BLEND_MODE_SCREEN

	# Shafts of light, done with the engine's own volumetrics rather than a screen-space
	# trick. A screen-space godray needs the sun to be on screen and vanishes the moment it
	# leaves; fog in the light does not, and it is also what the light is actually doing.
	environment.volumetric_fog_enabled = true
	environment.volumetric_fog_density = 0.012
	environment.volumetric_fog_albedo = Color(0.9, 0.92, 1.0)
	environment.volumetric_fog_length = 128.0
	environment.volumetric_fog_gi_inject = 0.0

	var camera_env := WorldEnvironment.new()
	camera_env.name = "WorldEnvironment"
	camera_env.environment = environment
	add_child(camera_env)
	if _weather != null:
		_weather.set_environment(environment)
	_built["sky"] = named


## The weather the map declares: its sun, its rain, and where its thunder is set off.
##
## All of it read out of the entity list rather than authored. de_aztec is a storm and has been
## saying so since 1999 in keys nothing had ever looked at.
func _build_weather(entities: Array, map: Node3D) -> void:
	_weather = WeatherScript.new()
	_weather.name = "Weather"
	add_child(_weather)

	_build_rooms(entities)
	_weather.set_storm_sky(_storm_sky)
	var sun: DirectionalLight3D = _weather.build_sun(entities)
	if sun == null:
		print("[lab] %s declares no light_environment, so it has no sun" % _map_uri.get_file())

	# Where the sky says the sun is, which beats where the map says when there is a sky showing
	# one. A visible sun and a light coming from somewhere else is the single thing that makes
	# a scene read as assembled rather than photographed, and the map's light_environment was
	# written for a different sky than the one now behind it.
	var from_sky := SkyLoader.sun_direction(_map_uri.get_file().get_basename())
	if from_sky != Vector3.ZERO and sun != null:
		# A light points where it shines, so it looks from the sun back toward the world.
		sun.look_at_from_position(Vector3.ZERO, -from_sky, Vector3.UP)
		_built["sun"] = "%.0f deg up, from the sky itself" % rad_to_deg(asin(from_sky.y))
		print("[lab] sun found in the panorama: %.1f deg above the horizon, %s"
			% [rad_to_deg(asin(from_sky.y)), str(from_sky.snappedf(0.01))])
		if _storm_sky != null and _storm_sky.sky_material is ShaderMaterial:
			var material := _storm_sky.sky_material as ShaderMaterial
			material.set_shader_parameter("sun_direction", from_sky)
			material.set_shader_parameter("sun_size", 0.0006)

	# How big the level is, for deciding where rain belongs.
	var extent := AABB(Vector3.ZERO, Vector3(60, 30, 60))
	for child in map.get_children():
		if child is MeshInstance3D:
			extent = (child as MeshInstance3D).get_aabb()
			break
	# env_rain's own brush is NOT where the rain falls, which cost a round trip to find out.
	# Measured on de_aztec it is 1.63 m on a side - sixty-four units, a marker cube. In
	# Counter-Strike env_rain is a switch meaning "it rains on this level", not a volume
	# describing where. Reading its bounds and believing them turned the weather into a
	# column of water over the player's head.
	_weather.build_rain(entities, extent)
	_build_water(entities)

	# Where walking sets the storm off. A trigger_multiple targeting a multi_manager that
	# fires the thunder: two places on this map, and forty-five seconds between strikes.
	for e in entities:
		var entity: Dictionary = e
		if str(entity.get("classname", "")) != "trigger_multiple":
			continue
		# The manager it points at is what actually holds the sound, so a trigger aimed
		# anywhere else is somebody's door and not the weather.
		if not _fires_thunder(entities, str(entity.get("target", ""))):
			continue

		# The volume itself now, not a point near it. A trigger_multiple is a brush entity and
		# where it is IS its brush model, which the importer resolves and publishes as
		# "bounds". Before that this could only fall back to the map's interval and crack the
		# storm on a timer: the right number and the wrong half of what the mapper meant.
		if entity.has("bounds"):
			_thunder_zones.append(entity["bounds"] as AABB)
		elif entity.has("position"):
			var point: Vector3 = entity["position"]
			_thunder_zones.append(AABB(point - Vector3.ONE * 8.0, Vector3.ONE * 16.0))
		_thunder_interval = maxf(float(entity.get("wait", 45.0)), 5.0)

	for named in _triggered_sounds:
		# The one the multi_manager fires. On this map there is exactly one and it is called
		# "boom", but the name is the mapper's and not something to hard-code.
		_weather.arm_thunder(_triggered_sounds[named])
		break

	if not _thunder_zones.is_empty():
		_built["storm"] = "thunder in %d place(s)" % _thunder_zones.size()
		print("[lab] %s: %d volume(s) set off thunder, %d seconds apart"
			% [_map_uri.get_file(), _thunder_zones.size(), int(_thunder_interval)])
	else:
		print("[lab] %s sets off no thunder" % _map_uri.get_file())


## Follow this sound, so a wall between it and the listener is heard as a wall.
func track_sound(sound: AudioStreamPlayer3D) -> void:
	if _acoustics != null:
		_acoustics.track(sound)
	else:
		_pending_tracked.append(sound)


## What the level says it sounds like, room by room.
##
## env_sound is a point with a radius and a room type from 0 to 28, and standing inside one
## changes how everything sounds: a gunshot in a tunnel is not a gunshot in the open.
## cs_siege places forty of them and de_dust two, and every import until now read none, so
## every level sounded like a field.
func _build_rooms(entities: Array) -> void:
	for e in entities:
		var entity: Dictionary = e
		if str(entity.get("classname", "")) != "env_sound" or not entity.has("position"):
			continue
		var kind := int(entity.get("roomtype", 0))
		var named := RoomAcoustics.name_of(kind, EntityCatalogue)
		_rooms.append({
			"at": entity["position"] as Vector3,
			# The map's radius is in its own units; a Hammer unit is an inch.
			"radius": maxf(float(entity.get("radius", 500.0)) * 0.0254, 1.0),
			"type": kind,
			"name": named,
			"reverb": RoomAcoustics.reverb_for(kind, named),
		})
	if _rooms.is_empty():
		return

	var counted := {}
	for room in _rooms:
		counted[str(room["name"])] = int(counted.get(str(room["name"]), 0)) + 1
	var parts := PackedStringArray()
	for label in counted:
		parts.append("%d %s" % [counted[label], label])
	_built["rooms"] = "%d (%s)" % [_rooms.size(), ", ".join(parts)]
	print("[lab] %s declares %d room(s): %s"
		% [_map_uri.get_file(), _rooms.size(), ", ".join(parts)])


## Put the player in whichever room the level says they are in.
##
## Three rules, all the game's own, out of CEnvSound::Think in ReGameDLL's sound.cpp:
##
##   It sticks. "A client's room_type will remain set to its prior value until a new
##   in-range, visible sound entity resets a new room_type." Walking out of one changes
##   nothing; walking into another does. That is why the radii are a metre or two — these are
##   markers you pass through, not volumes you stand in, and reading them as volumes meant
##   the reverberation flickered on and off as you crossed a doorway.
##
##   You have to see it. FEnvSoundInRange traces from the entity to the player and gives up
##   if anything is in the way.
##
##   The nearest of several wins.
func _update_room() -> void:
	if _acoustics == null or _player == null or _rooms.is_empty():
		return

	var ear := _player.global_position
	var space := get_world_3d().direct_space_state
	var best := -1
	var best_range := INF

	for i in _rooms.size():
		var room: Dictionary = _rooms[i]
		var at: Vector3 = room["at"]
		var distance := ear.distance_to(at)
		if distance > float(room["radius"]) or distance >= best_range:
			continue
		var query := PhysicsRayQueryParameters3D.create(at, ear)
		query.exclude = [_player.get_rid()]
		if not space.intersect_ray(query).is_empty():
			continue   # something in the way; the game does not hear this one either
		best = i
		best_range = distance

	# Nothing in range and visible leaves the room as it was, which is the sticky part.
	if best < 0 or best == _room_now:
		return
	_room_now = best
	_acoustics.set_room((_rooms[best] as Dictionary)["reverb"])
	print("[lab] room: %s" % str((_rooms[best] as Dictionary)["name"]))


## The water the map declares, as something you can see rather than an invisible brush.
##
## func_water is a brush entity: a box of water with a surface at the top of it. de_aztec has
## five, and until brush models were resolved there was no way to know where any of them were.
## The geometry is already in the level; this puts a surface on it that reads as water.
func _build_water(entities: Array) -> void:
	var pools := 0
	for e in entities:
		var entity: Dictionary = e
		if str(entity.get("classname", "")) != "func_water" or not entity.has("bounds"):
			continue
		var box: AABB = entity["bounds"]
		if box.size.x < 0.1 or box.size.z < 0.1:
			continue

		var surface := MeshInstance3D.new()
		surface.name = "Water%d" % pools
		var plane := PlaneMesh.new()
		plane.size = Vector2(box.size.x, box.size.z)
		# Enough for a wave to have somewhere to go, without a mesh nobody is looking at
		# costing more than the level around it.
		plane.subdivide_width = 24
		plane.subdivide_depth = 24
		surface.mesh = plane

		surface.material_override = _water_material()

		# Just above the top of the box, which is where a body of water's surface is. The
		# millimetre is to stay off the level's own water face: two coplanar surfaces flicker
		# against each other, which is the white shards that appeared over the first attempt.
		surface.position = Vector3(box.position.x + box.size.x * 0.5,
			box.position.y + box.size.y + 0.01, box.position.z + box.size.z * 0.5)
		add_child(surface)
		pools += 1

	if pools > 0:
		_built["water"] = "%d pool(s)" % pools
		print("[lab] %s: %d body/bodies of water" % [_map_uri.get_file(), pools])


## One material for every pool, so five surfaces are one shader and one set of parameters.
func _water_material() -> ShaderMaterial:
	if _water_shader == null:
		_water_shader = load("res://lab/water.gdshader")
	var material := ShaderMaterial.new()
	material.shader = _water_shader
	# Rain roughens a surface and breaks up what it reflects. A mirror-flat pool in a
	# downpour is the single thing that gives away water that was not thought about.
	material.set_shader_parameter("agitation", 0.7)
	return material


## Does what this trigger points at end up playing a sound?
##
## A trigger_multiple names a multi_manager, and the manager names the ambient_generic. Two
## hops, and following them is the difference between "the map has triggers" and "these are
## the ones that make it thunder".
func _fires_thunder(entities: Array, target: String) -> bool:
	if target.is_empty():
		return false
	for e in entities:
		var manager: Dictionary = e
		if str(manager.get("classname", "")) != "multi_manager":
			continue
		if str(manager.get("targetname", "")) != target:
			continue
		for key in manager:
			for f in entities:
				var sound: Dictionary = f
				if str(sound.get("classname", "")) != "ambient_generic":
					continue
				if str(sound.get("targetname", "")) == str(key):
					return true
	return false


# -------------------------------------------------------------------- people ----

## Stand a body on the floor beneath a point, by asking the collider where it actually is.
##
## Nothing here is calculated from where the capsule ought to be. A character's origin is not
## its lowest point — in GoldSrc it is the centre of the hull, most of a metre up — and three
## separate attempts at this buried or floated everybody by assuming otherwise.
func _stand_on_floor(body: CharacterBody3D, at: Vector3) -> void:
	body.global_position = at

	var space := get_world_3d().direct_space_state
	var query := PhysicsRayQueryParameters3D.create(at + Vector3.UP * 2.0, at + Vector3.DOWN * 60.0)
	query.exclude = [body.get_rid()]
	var hit := space.intersect_ray(query)
	if hit.is_empty():
		return

	var lowest := INF
	for child in body.get_children():
		if child is CollisionShape3D:
			var shape := child as CollisionShape3D
			var half := 0.0
			if shape.shape is CapsuleShape3D:
				half = (shape.shape as CapsuleShape3D).height * 0.5
			lowest = minf(lowest, body.global_position.y + shape.position.y - half)
	if lowest == INF:
		return

	# Move by exactly the gap between where the collider's bottom is and where the floor is.
	body.global_position.y += (hit["position"] as Vector3).y - lowest + 0.02


func _place_player() -> void:
	var preset := ModelPreset.find("urban", PRESETS)
	var weapon := ModelPreset.find("v_ak47", PRESETS)
	var body := ModelPreset.spawn(importer, vfs, preset)
	if body == null or not (body is CharacterBody3D):
		_say("The player preset could not be built.")
		return

	_player = body as CharacterBody3D
	_player.name = "Player"
	_player.set_script(PlayerScript)
	add_child(_player)
	_stand_on_floor(_player, _spawn_point())

	var camera := Camera3D.new()
	camera.name = "Camera3D"
	camera.near = 0.04
	camera.current = true
	_player.add_child(camera)

	# Your own body is not drawn from your own eyes: R_DrawViewModel returns early in first
	# person and the player entity is never rendered. It stays in the scene casting its
	# shadow, which is what makes a figure feel present without being a wall in front of you.
	for child in _player.get_children():
		if child is Skeleton3D:
			for m in child.get_children():
				if m is MeshInstance3D:
					(m as MeshInstance3D).cast_shadow = \
						GeometryInstance3D.SHADOW_CASTING_SETTING_SHADOWS_ONLY

	_acoustics = AcousticsScript.new()
	_acoustics.name = "Acoustics"
	add_child(_acoustics)
	_acoustics.setup(camera)
	for waiting in _pending_tracked:
		_acoustics.track(waiting)
	_pending_tracked.clear()

	_player.setup(self, camera, preset, weapon)
	_built["player"] = "%s with %s" % [preset.get("name", "?"), weapon.get("name", "?")]


func _place_enemies() -> void:
	var people := ["guerilla", "arctic", "leet", "terror", "gign", "sas", "gsg9"]
	var guns := ["v_ak47", "v_mp5", "v_deagle", "v_m4a1"]
	var placed := 0

	for i in ENEMY_COUNT:
		var preset := ModelPreset.find(people[_rng.randi_range(0, people.size() - 1)], PRESETS)
		var weapon := ModelPreset.find(guns[_rng.randi_range(0, guns.size() - 1)], PRESETS)
		var body := ModelPreset.spawn(importer, vfs, preset)
		if body == null or not (body is CharacterBody3D):
			continue

		var npc := body as CharacterBody3D
		npc.set_script(NpcScript)
		add_child(npc)
		_stand_on_floor(npc, _spawn_point())
		npc.setup(self, _player, preset, weapon)
		placed += 1

	_built["enemies"] = "%d" % placed


func _process(delta: float) -> void:
	if Input.is_key_pressed(KEY_F3) and not _cleared:
		# Quiet, for looking at a level without being shot at while doing it.
		_cleared = true
		for n in get_children():
			if n is CharacterBody3D and n != _player and n.has_method("take_damage"):
				n.take_damage(1000)
		print("[lab] enemies cleared")
	elif not Input.is_key_pressed(KEY_F3):
		_cleared = false

	if _weather == null or _player == null or not is_instance_valid(_player):
		return
	_weather.follow(_player)
	_update_room()

	_thunder_ready -= delta
	if _thunder_ready > 0.0:
		return

	for zone in _thunder_zones:
		if (zone as AABB).has_point(_player.global_position):
			# The trigger's own wait, which is what stops it firing every frame you stand in it.
			_thunder_ready = _thunder_interval
			_weather.strike()
			return


func _spawn_point() -> Vector3:
	if _spawns.is_empty():
		return Vector3(0, 4, 0)
	return _spawns[_rng.randi_range(0, _spawns.size() - 1)]


# ----------------------------------------------------------------------- HUD ----

func _build_hud() -> void:
	var layer := CanvasLayer.new()
	add_child(layer)
	_hud = Label.new()
	_hud.position = Vector2(12, 10)
	_hud.add_theme_color_override("font_color", Color(1, 1, 1))
	_hud.add_theme_color_override("font_outline_color", Color(0, 0, 0))
	_hud.add_theme_constant_override("outline_size", 4)
	layer.add_child(_hud)


func _say(text: String) -> void:
	if _hud != null:
		_hud.text = text
	print("[lab] %s" % text)


func _refresh_hud() -> void:
	var lines := PackedStringArray()
	for key in _built:
		lines.append("%-10s %s" % [key, str(_built[key])])
	if _player != null and is_instance_valid(_player) and _player.has_method("is_immortal") 			and not _player.is_immortal():
		lines.append("health     %d" % _player.health)
	lines.append("")
	lines.append("WASD move · mouse look · click shoot · R reload")
	lines.append("Space jump · Ctrl crouch · Shift walk · Esc cursor")
	lines.append("V noclip · G god mode (on) · F3 kill the enemies")
	_hud.text = "\n".join(lines)
