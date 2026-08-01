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
const PlayerScript := preload("res://lab/player.gd")
const NpcScript := preload("res://lab/npc.gd")

## The game's own content folder. Steam keeps Half-Life, Counter-Strike, Condition Zero and
## its Deleted Scenes inside one folder called Half-Life, so mounting "the game" mounts four
## of them and the result is a mixture nobody can judge.
const GAME := "cstrike"
const PRESETS := "Counter-Strike 1.6"

## Which map to open.
##
## Not de_dust2, and the reason is worth writing down: of the 25 maps Counter-Strike ships,
## exactly five declare no sound at all, and de_dust2 is one of them. Testing on it meant the
## ambient sound path was never once exercised, and its silence read as a defect for weeks.
##
## de_aztec has rain and thunder. de_torn has a hundred ambient_generic entities, de_piranesi
## ninety-six, cs_italy an opera and some chickens.
const MAP := "de_aztec"

const ENEMY_COUNT := 3

var vfs: VFSManager
var importer: UnifiedAssetImporter

var _player: CharacterBody3D
var _map_uri := ""
var _spawns: Array[Vector3] = []
var _hud: Label
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
		node.autoplay = true
		# health is the volume, out of ten, which is a fact about the game that the map file
		# never states and the entity schema does.
		var volume := clampf(float(entity.get("health", 10)) / 10.0, 0.05, 1.0)
		node.volume_db = linear_to_db(volume)
		if (flags & PLAY_EVERYWHERE) != 0:
			node.attenuation_model = AudioStreamPlayer3D.ATTENUATION_DISABLED
		node.position = entity.get("position", Vector3.ZERO)
		add_child(node)
		placed += 1

	_built["ambience"] = "%d sound(s)" % placed
	print("[lab] %s: %d ambient sound(s) playing" % [_map_uri.get_file(), placed])


func _apply_sky(entities: Array) -> void:
	for e in entities:
		var entity: Dictionary = e
		if str(entity.get("classname", "")) == "worldspawn" and entity.has("skyname"):
			_built["sky"] = str(entity["skyname"])
			return


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
	lines.append("")
	lines.append("WASD move · mouse look · click shoot · R reload")
	lines.append("Space jump · Ctrl crouch · Shift walk · Esc cursor")
	_hud.text = "\n".join(lines)
