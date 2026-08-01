extends Node3D

## A playable scene built entirely out of games you already own.
##
## Press play. It mounts what it finds installed, picks a map, drops you into it with a
## weapon, and puts armed enemies in the level with you. Nothing here is authored: every
## model, sound and animation is read out of a retail install at startup.
##
## This exists to answer a question the rest of the project cannot: whether what the
## importer hands back is actually usable in a game, as opposed to correct when inspected.
## A model with a collider that will not fit through a door, a weapon sound that decodes to
## silence, an animation whose track path resolves to nothing: all of those pass a unit test
## and fail here.
##
## Controls: WASD to move, mouse to look, left click to shoot, Escape to free the cursor.

const PlayerScript := preload("res://sandbox/player.gd")
const NpcScript := preload("res://sandbox/npc.gd")

## How many enemies to place. Enough to be outnumbered, few enough that a debug build of the
## importer does not spend a minute loading them.
const ENEMY_COUNT := 4

## Name a map here to always load that one, or leave it empty to take a readable map at
## random. Matched loosely, so "de_dust" finds de_dust2 as well.
const FORCE_MAP := "de_survivor"

var vfs: VFSManager
var importer: UnifiedAssetImporter

var _map: Node3D
## Where the map says people begin, by classname. Counter-Strike puts counter-terrorists at
## info_player_start and terrorists at info_player_deathmatch, and uses about thirteen of
## each; Half-Life uses info_player_start for the single player and only writes the
## deathmatch ones into its deathmatch maps. Surveyed rather than assumed, in
## demo/tests/verify_entities.gd.
var _spawns := {}
var _player: CharacterBody3D
var _hud: Label
var _rng := RandomNumberGenerator.new()

## Everything the level was built from, so the HUD can say what you are looking at.
var _built := {}


func _ready() -> void:
	_rng.randomize()

	vfs = VFSManager.new()
	importer = UnifiedAssetImporter.new()
	add_child(vfs)
	add_child(importer)
	importer.set_vfs(vfs)

	_build_hud()
	_say("Mounting your games...")
	await get_tree().process_frame

	var mounted := _mount_installed_games()
	if mounted == 0:
		_say("No games found.\nQuebratsk needs Half-Life, Counter-Strike 1.6,\nHalf-Life 2 or Garry's Mod installed.")
		return

	if not _load_random_map():
		_say("None of the maps in your games could be read.\nQuebratsk reads GoldSrc maps: Half-Life and Counter-Strike 1.6.")
		return

	_spawn_player()
	_spawn_enemies()
	_refresh_hud()


# ------------------------------------------------------------------- mounting ----

func _mount_installed_games() -> int:
	var games: Dictionary = SteamLibraryDetector.detect_installed_games()
	var mounted := 0
	for title in games:
		var root := str(games[title])
		var scan: Dictionary = vfs.scan_game_directory(root)
		var n := 0
		for archive in scan.get("archives", []):
			if vfs.mount_container("g%d_%d" % [mounted, n], str(archive)):
				n += 1
		# The loose tree matters as much as the archives: GoldSrc keeps its maps as plain
		# .bsp files beside the game rather than inside a .wad.
		vfs.mount_directory("g%d_loose" % mounted, root)
		mounted += 1
	mounted += _mount_workshop(mounted)
	_built["games"] = games.keys()
	return mounted


## Workshop addons, which are where most of what anyone actually has lives.
##
## Steam keeps them outside the game folder entirely, under steamapps/workshop/content/<id>,
## so scanning the game directory finds none of them. A Garry's Mod install with fifteen
## subscribed addons has fifteen .gma archives that the game reads and this did not.
func _mount_workshop(from_index: int) -> int:
	var root := "C:/Program Files (x86)/Steam/steamapps/workshop/content"
	if not DirAccess.dir_exists_absolute(root):
		return 0

	var mounted := 0
	for app in DirAccess.get_directories_at(root):
		var app_dir := root.path_join(str(app))
		for item in DirAccess.get_directories_at(app_dir):
			var scan: Dictionary = vfs.scan_game_directory(app_dir.path_join(str(item)))
			for archive in scan.get("archives", []):
				if vfs.mount_container("w%d" % (from_index + mounted), str(archive)):
					mounted += 1
	if mounted > 0:
		print("[sandbox] mounted %d workshop archive(s)" % mounted)
	return mounted


func _pick(uris: PackedStringArray) -> String:
	if uris.is_empty():
		return ""
	return str(uris[_rng.randi_range(0, uris.size() - 1)])


# ----------------------------------------------------------------------- level ----

func _load_random_map() -> bool:
	var needle := "maps/" if FORCE_MAP.is_empty() else FORCE_MAP
	var hit: Dictionary = vfs.find_files(needle, PackedStringArray(["bsp"]),
		PackedStringArray(), PackedStringArray(), 0)
	var candidates: PackedStringArray = hit["files"]

	if not FORCE_MAP.is_empty() and candidates.is_empty():
		print("[sandbox] no map matching '%s' is installed; taking any readable one"
			% FORCE_MAP)
		hit = vfs.find_files("maps/", PackedStringArray(["bsp"]),
			PackedStringArray(), PackedStringArray(), 0)
		candidates = hit["files"]

	# Try a handful rather than one: Source maps are in this list too and none of them
	# decode, so the first pick is often unreadable through no fault of the map.
	for attempt in 12:
		var uri := _pick(candidates)
		if uri.is_empty():
			return false
		var map: Node3D = importer.load_map(uri)
		if map == null:
			continue
		_map = map
		add_child(map)
		_built["map"] = uri.get_file()
		_read_spawns(uri)
		return true
	return false


## Sort the map's own player starts by classname.
##
## This is the difference between putting a fight where the level was built for one and
## scattering it wherever a downward ray happened to hit. A ray finds floor; it does not
## know that the floor is inside a wall cavity, or that the level's designer put the spawns
## on the other side of the map facing each other.
func _read_spawns(uri: String) -> void:
	_spawns.clear()
	for e in importer.load_map_entities(uri):
		var entity: Dictionary = e
		var cls := str(entity.get("classname", ""))
		if not entity.has("position") or not cls.begins_with("info_player"):
			continue
		if not _spawns.has(cls):
			_spawns[cls] = []
		# Raised slightly: a spawn point is written at floor level and a body placed exactly
		# there starts the frame intersecting the ground.
		_spawns[cls].append((entity["position"] as Vector3) + Vector3(0, 0.2, 0))

	var summary := []
	for cls in _spawns:
		summary.append("%d %s" % [(_spawns[cls] as Array).size(), cls])
	print("[sandbox] %s declares: %s"
		% [uri.get_file(), "no player starts" if summary.is_empty() else ", ".join(summary)])


## One of the map's own spawn points for the given side, or Vector3.INF if it has none.
##
## `preferred` is tried first and the other classname second, so a map that only writes one
## kind still puts everybody somewhere sensible rather than nobody anywhere.
func _spawn_for(preferred: String, taken: Array) -> Vector3:
	var order := [preferred]
	for cls in _spawns:
		if str(cls) != preferred:
			order.append(str(cls))

	for cls in order:
		if not _spawns.has(cls):
			continue
		var points: Array = (_spawns[cls] as Array).duplicate()
		points.shuffle()
		for point in points:
			var spot: Vector3 = point
			var clear := true
			for other in taken:
				if spot.distance_to(other as Vector3) < 1.5:
					clear = false
					break
			if clear:
				return spot
	return Vector3.INF


## A point just above a floor somewhere inside the level.
##
## Maps record their player starts as entities and this importer reads geometry only, so
## there is nothing to ask. A GoldSrc BSP is also a sealed box, which means anything
## released above the map lands on its outer shell and is walled in on every side, so the
## search has to happen from inside.
func _floor_inside(exclude: Array = [], min_apart := 4.0, near := Vector3.INF,
		within := 0.0) -> Vector3:
	if _map == null:
		return Vector3.INF
	var box: AABB = (_map as MeshInstance3D).get_aabb()
	var space := get_world_3d().direct_space_state

	for attempt in 120:
		var x: float = box.position.x + box.size.x * _rng.randf_range(0.15, 0.85)
		var z: float = box.position.z + box.size.z * _rng.randf_range(0.15, 0.85)
		var query := PhysicsRayQueryParameters3D.create(
			Vector3(x, box.end.y - 1.0, z), Vector3(x, box.position.y, z))
		var hit := space.intersect_ray(query)
		if hit.is_empty():
			continue

		var ground: Vector3 = hit["position"]
		# Room to stand up in. Without this the search happily returns the inside of a
		# crawlspace, and everything spawned there is stuck the moment it appears.
		var headroom := PhysicsRayQueryParameters3D.create(
			ground + Vector3(0, 0.2, 0), ground + Vector3(0, 2.4, 0))
		if not space.intersect_ray(headroom).is_empty():
			continue

		var spot := ground + Vector3(0, 0.15, 0)
		# Somewhere you will actually run into. Scattered across a whole map, four enemies
		# sit in rooms you never enter: the first playtest was shot at from places the
		# player never saw, which is not a fight.
		if near != Vector3.INF and within > 0.0:
			# Flat distance and a height limit, not distance in three dimensions. A
			# multi-storey map puts "16 m away" two floors below you: the first playtest
			# had enemies at y=-22 shooting at a player standing at y=-5, which reads as
			# being killed by nobody.
			var flat := Vector2(spot.x - near.x, spot.z - near.z).length()
			if flat > within or absf(spot.y - near.y) > 2.5:
				continue

		var too_close := false
		for other in exclude:
			if spot.distance_to(other as Vector3) < min_apart:
				too_close = true
				break
		if not too_close:
			return spot
	return Vector3.INF


# ---------------------------------------------------------------------- actors ----

func _spawn_player() -> void:
	# info_player_start is where Counter-Strike puts the counter-terrorists and where
	# Half-Life puts the player. Falling back to a raycast only when the map declares
	# nothing at all, which on a GoldSrc map is rare.
	var spot := _spawn_for("info_player_start", [])
	if spot == Vector3.INF:
		spot = _floor_inside()
	if spot == Vector3.INF:
		_say("Could not find anywhere to stand in that map.")
		return

	_player = CharacterBody3D.new()
	_player.name = "Player"
	_player.set_script(PlayerScript)

	var shape := CollisionShape3D.new()
	var capsule := CapsuleShape3D.new()
	capsule.height = 1.8
	capsule.radius = 0.3
	shape.shape = capsule
	shape.position = Vector3(0, 0.9, 0)
	_player.add_child(shape)

	var camera := Camera3D.new()
	camera.name = "Camera3D"
	camera.position = Vector3(0, 1.65, 0)   # eye height
	camera.current = true
	_player.add_child(camera)

	add_child(_player)
	_player.position = spot
	_player.setup(self, camera)

	var weapon := _give_weapon(camera, Vector3(0.18, -0.16, -0.5), 0.42)
	_built["weapon"] = weapon


## Attach a random weapon model, and pick a firing sound to go with it.
##
## View models (v_*) are the ones held in front of a camera; world models (w_*) are what a
## weapon looks like lying on the ground or in someone's hands. Either will do, and which
## exist depends on which games are installed.
func _give_weapon(mount: Node3D, offset: Vector3, apparent_size := 0.4) -> String:
	var hit: Dictionary = vfs.find_files("weapons/", PackedStringArray(["mdl"]),
		PackedStringArray(["vvd", "vtx", "ani", "phy"]), PackedStringArray(), 0)

	# Not everything under weapons/ is a weapon. c_arms_citizen.mdl is a pair of forearms
	# meant to be composited with a gun, and on its own it is two hands floating in the air,
	# which is what the first playtest was handed to shoot with.
	var guns := PackedStringArray()
	for entry in hit["files"]:
		var file := str(entry).get_file().to_lower()
		var rejected := false
		for word in ["arms", "hands", "hand_", "sleeve", "shell", "casing", "ammo"]:
			if file.contains(str(word)):
				rejected = true
				break
		if not rejected:
			guns.append(str(entry))
	if guns.is_empty():
		guns = hit["files"]

	for attempt in 10:
		var uri := _pick(guns)
		if uri.is_empty():
			break
		var model: Node3D = importer.load_model(uri)
		if model == null:
			continue

		# A weapon in someone's hands is scenery, not an obstacle: leaving its collider on
		# would have the holder shooting their own gun.
		for child in model.get_children():
			if child is StaticBody3D:
				model.remove_child(child)
				child.queue_free()

		# Scaled to how big it should look, not by a fixed factor. A GoldSrc view model is
		# authored huge because the engine draws it through a separate projection; dropped
		# into a scene at anything near 1:1 it fills the screen, which is what the first
		# run of this did.
		var longest := 0.0
		var box_size := Vector3.ZERO
		for child in model.get_children():
			if child is MeshInstance3D:
				var box: AABB = (child as MeshInstance3D).get_aabb()
				box_size = box.size
				longest = maxf(longest, maxf(box.size.x, maxf(box.size.y, box.size.z)))
		if model is MeshInstance3D:
			var own: AABB = (model as MeshInstance3D).get_aabb()
			box_size = own.size
			longest = maxf(longest, maxf(own.size.x, maxf(own.size.y, own.size.z)))

		model.position = offset
		if longest > 0.01:
			model.scale = Vector3.ONE * (apparent_size / longest)

		# Turn its long axis to face forward. A view model is authored pointing along
		# whichever axis its own engine drew it down, and after the conversion to Godot's
		# axes that is rarely -Z: dropped in unrotated the gun lies across the view
		# sideways, which is what the first run looked like.
		model.rotation = Vector3.ZERO
		if box_size.x > box_size.z:
			model.rotate_y(PI * 0.5)
		mount.add_child(model)
		return uri.get_file()
	return ""


func _spawn_enemies() -> void:
	# Every game lays its characters out differently, so one path pattern finds one game's
	# worth. Half-Life keeps its NPCs loose in models/, Counter-Strike puts each player
	# model in its own folder, Half-Life 2 and Garry\'s Mod use models/player/, and a
	# workshop addon can put them anywhere at all.
	const WHERE_PEOPLE_LIVE := [
		"models/player/", "/humans/", "/npc_", "/zombie",
		"scientist", "barney", "hgrunt", "gordon", "gman", "hassault",
		"leet", "arctic", "guerilla", "militia", "gsg9", "sas", "urban", "phoenix",
	]
	var pool := PackedStringArray()
	var seen := {}
	for where in WHERE_PEOPLE_LIVE:
		var found: Dictionary = vfs.find_files(where, PackedStringArray(["mdl"]),
			PackedStringArray(["vvd", "vtx", "ani", "phy"]), PackedStringArray(), 0)
		for entry in found["files"]:
			if not seen.has(str(entry)):
				seen[str(entry)] = true
				pool.append(str(entry))
	var hit := {"files": pool}
	print("[sandbox] %d candidate characters across every mounted game" % pool.size())

	var taken: Array = [] if _player == null else [_player.position]
	var names: Array = []

	var placed := 0
	var attempts := 0
	while placed < ENEMY_COUNT and attempts < ENEMY_COUNT * 6:
		attempts += 1
		var uri := _pick(hit["files"])
		if uri.is_empty():
			break

		# Is this a person? A playable character carries hundreds of sequences, because it
		# has to stand, walk, crouch, reload and die. A vent grille carries one. Without
		# this the pool of "characters" included Vine_wall2 and vent01, and both were duly
		# spawned as enemies.
		if importer.list_poses(uri).size() < 20:
			continue

		# Everything the enemy will need to do, imported together. Standing, walking,
		# running, firing and dying: with only one of them it slides toward you frozen in a
		# single pose, which is what the first playtest looked like.
		var moves := _animation_set(uri)
		var sequences := PackedStringArray()
		for role in moves:
			sequences.append(str(moves[role]))
		var who: Node3D = importer.load_character(uri, "", sequences)
		if who == null or not (who is CharacterBody3D):
			if who != null:
				who.queue_free()
			continue

		# The other team's spawn points. On a Counter-Strike map that is the far side of
		# the level facing you, which is what the level was designed around.
		var spot := _spawn_for("info_player_deathmatch", taken)
		if spot == Vector3.INF:
			spot = _floor_inside(taken, 4.0,
				_player.position if _player != null else Vector3.INF, 18.0)
		if spot == Vector3.INF:
			who.queue_free()
			break

		who.set_script(NpcScript)
		add_child(who)
		who.position = spot
		taken.append(spot)
		names.append("%-22s %s" % [uri.get_file(), _which_game(uri)])

		var weapon := _give_weapon(who, Vector3(0.25, 1.3, -0.25), 0.7)
		(who as CharacterBody3D).setup(self, _player, moves, weapon)
		placed += 1

	_built["enemies"] = names

	# Where they actually ended up, and whether they have anything to draw. "The enemies
	# were invisible" has two very different causes and this separates them.
	for child in get_children():
		if child is CharacterBody3D and child != _player:
			var skeleton := child.get_node_or_null("Skeleton3D")
			var meshes := 0
			if skeleton != null:
				for grandchild in skeleton.get_children():
					if grandchild is MeshInstance3D:
						meshes += 1
			print("[sandbox] %s at %s, %.1f m away, %d mesh(es)"
				% [child.name, str(child.position.round()),
				   0.0 if _player == null else child.position.distance_to(_player.position),
				   meshes])


## One sequence per thing an enemy does, chosen from whatever the model happens to carry.
##
## Returns { "idle": name, "walk": name, ... }, with missing roles simply absent. Sequence
## naming is not standardised across twenty-five years of games, so each role is matched
## against the words that tend to be used for it.
##
## Deliberately a handful rather than all 426 a player model carries: a sequence costs a
## keyframe per bone per frame, so importing everything would be tens of megabytes per
## enemy for animations nobody will ever see.
func _animation_set(uri: String) -> Dictionary:
	var poses: PackedStringArray = importer.list_poses(uri)
	var wanted := {
		"idle": ["idle_all_01", "idle_all", "idle1", "idle"],
		"walk": ["walk_all", "walkall", "walk1", "walk"],
		"run": ["run_all", "runall", "run1", "run"],
		"shoot": ["shoot", "fire", "attack"],
		"die": ["die_simple", "death", "die", "dead"],
	}

	var chosen := {}
	for role in wanted:
		for hint in wanted[role]:
			var found := ""
			for p in poses:
				if str(p).to_lower().begins_with(str(hint)):
					found = str(p)
					break
			if found != "":
				chosen[role] = found
				break
	return chosen


## Which mount a URI came out of, in words. With models drawn from four games and a pile of
## workshop addons, "male_02.mdl" alone does not say whether you are looking at Half-Life 2
## or something somebody uploaded.
func _which_game(uri: String) -> String:
	var rest := uri.trim_prefix("vfs://")
	var slash := rest.find("/")
	if slash < 0:
		return ""
	var prefix := rest.substr(0, slash)
	for info in vfs.get_mounts_info():
		var entry: Dictionary = info
		if str(entry.get("prefix", "")) != prefix:
			continue
		var path := str(entry.get("real_path", ""))
		if path.contains("workshop"):
			return "workshop"
		for known in ["Half-Life 2", "GarrysMod", "cstrike", "Half-Life", "Team Fortress"]:
			if path.contains(known):
				return known
		return path.get_base_dir().get_file()
	return ""


## A gunshot from whatever is mounted. Different every time, like everything else here.
func find_gunshot() -> AudioStream:
	for needle in ["weapons/", "wpn_", "gun", "shoot", "fire"]:
		var hit: Dictionary = vfs.find_files(needle, PackedStringArray(["wav"]),
			PackedStringArray(), PackedStringArray(), 0)
		for attempt in 8:
			var uri := _pick(hit["files"])
			if uri.is_empty():
				break
			var sound := QuebratskSoundLoader.load_sound(vfs, uri)
			if sound != null:
				return sound
	return null


# ------------------------------------------------------------------------- hud ----

func _build_hud() -> void:
	var layer := CanvasLayer.new()
	add_child(layer)

	_hud = Label.new()
	_hud.position = Vector2(16, 12)
	_hud.add_theme_color_override("font_color", Color(1, 1, 1))
	_hud.add_theme_color_override("font_outline_color", Color(0, 0, 0))
	_hud.add_theme_constant_override("outline_size", 6)
	layer.add_child(_hud)

	var crosshair := Label.new()
	crosshair.text = "+"
	crosshair.add_theme_font_size_override("font_size", 22)
	crosshair.add_theme_color_override("font_color", Color(1, 1, 1, 0.8))
	crosshair.set_anchors_preset(Control.PRESET_CENTER)
	layer.add_child(crosshair)


func _say(text: String) -> void:
	if _hud != null:
		_hud.text = text


func _refresh_hud() -> void:
	var enemies := 0
	for child in get_children():
		if child is CharacterBody3D and child != _player:
			enemies += 1

	var lines := []
	if _player != null:
		lines.append("Health  %d" % _player.health)
	lines.append("Enemies %d" % enemies)
	lines.append("")
	lines.append("Map     %s" % str(_built.get("map", "?")))
	lines.append("Weapon  %s" % str(_built.get("weapon", "unarmed")))
	for n in _built.get("enemies", []):
		lines.append("Enemy   %s" % str(n))
	lines.append("")
	lines.append("WASD move, mouse look, click to shoot, Esc for the cursor")
	if _player != null and _player.IMMORTAL:
		lines.append("(immortal: set IMMORTAL to false in player.gd for a real fight)")
	_say("\n".join(lines))


func on_state_changed() -> void:
	_refresh_hud()
	if _player != null and _player.health <= 0:
		_say("You are dead.\n\nEverything you were killed with came out of a game\nyou already had installed.")
