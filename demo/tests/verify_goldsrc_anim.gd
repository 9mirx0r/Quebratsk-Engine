extends Node

## Do Half-Life 1 and Counter-Strike 1.6 models animate?
##
## Until the GoldSrc parser learned to read sequences, every model from those games imported
## frozen in its bind pose: the parser produced exactly one pose, the modelling T-stance, and
## no amount of asking for "run1" changed that. Compiling proves nothing here, because the
## old behaviour compiled too and returned a perfectly valid skeleton.
##
## So this asks the two questions that the bug would answer wrongly: how many sequences does
## a model expose, and does a bone actually end up somewhere different at frame 20 than it
## was at frame 0.

var _vfs: VFSManager
var _importer: UnifiedAssetImporter


func _ready() -> void:
	_vfs = VFSManager.new()
	_importer = UnifiedAssetImporter.new()
	add_child(_vfs)
	add_child(_importer)
	_importer.set_vfs(_vfs)

	var games: Dictionary = SteamLibraryDetector.detect_installed_games()
	var n := 0
	for title in games:
		var root := str(games[title])
		var scan: Dictionary = _vfs.scan_game_directory(root)
		for archive in scan.get("archives", []):
			if _vfs.mount_container("g%d" % n, str(archive)):
				n += 1
		_vfs.mount_directory("loose%d" % n, root)
		n += 1

	print("\n=== GOLDSRC ANIMATION VERIFICATION ===")

	var hit: Dictionary = _vfs.find_files("models/", PackedStringArray(["mdl"]),
		PackedStringArray(), PackedStringArray(), 0)

	var checked := 0
	var with_sequences := 0
	var animating := 0
	var with_sounds := 0

	for entry in hit["files"]:
		if checked >= 12:
			break
		var uri := str(entry)
		if not _is_goldsrc(uri):
			continue

		var result: Dictionary = _check(uri)
		if result.is_empty():
			continue
		checked += 1
		if result["poses"] > 1:
			with_sequences += 1
		if result["moved"] > 0:
			animating += 1
		if result["sounds"] > 0:
			with_sounds += 1

	print("\n=== SUMMARY ===")
	print("  %d GoldSrc models read" % checked)
	print("  %d expose more than one sequence" % with_sequences)
	print("  %d have a sequence that actually moves a bone" % animating)
	print("  %d name a sound their animation plays" % with_sounds)
	if checked > 0 and animating == 0:
		print("  ** every model still stands still **")
	get_tree().quit()


## Both engines write "IDST" and only the version field after it tells them apart. Guessing
## from the filename or from which companion files exist gets it wrong often enough that the
## sample ends up mostly Source, which is the opposite of what is being checked here.
func _is_goldsrc(uri: String) -> bool:
	var bytes := _vfs.read_file(uri)
	if bytes.size() < 8:
		return false
	return bytes.slice(0, 4).get_string_from_ascii() == "IDST" and bytes.decode_s32(4) == 10


func _check(uri: String) -> Dictionary:
	var poses: PackedStringArray = _importer.list_poses(uri)
	if poses.is_empty():
		return {}

	# A sequence that holds a pose would not move a bone even with everything working, so
	# verifying against one proves nothing either way.
	var wanted := str(poses[0])
	for hint in ["run", "walk", "shoot", "fire", "reload", "draw"]:
		for p in poses:
			if str(p).to_lower().begins_with(hint):
				wanted = str(p)
				break
		if wanted != str(poses[0]):
			break

	var node: Node3D = _importer.load_model(uri, "", PackedStringArray([wanted]))
	if node == null:
		return {}
	add_child(node)

	var moved := 0
	var largest := 0.0
	var length := 0.0
	var keys := 0
	var skel := node as Skeleton3D
	var player: AnimationPlayer = node.get_node_or_null("AnimationPlayer")

	if skel != null and player != null and not player.get_animation_list().is_empty():
		var name := str(player.get_animation_list()[0])
		var anim: Animation = player.get_animation(name)
		length = anim.get_length()
		for i in anim.get_track_count():
			keys = maxi(keys, anim.track_get_key_count(i))

		player.play(name)
		var first := []
		player.seek(0.0, true)
		for b in skel.get_bone_count():
			first.append(skel.get_bone_pose_rotation(b))

		for frac in [0.3, 0.6, 0.9]:
			player.seek(length * frac, true)
			for b in skel.get_bone_count():
				var delta := rad_to_deg((first[b] as Quaternion).angle_to(
					skel.get_bone_pose_rotation(b)))
				largest = maxf(largest, delta)

		player.seek(length * 0.5, true)
		for b in skel.get_bone_count():
			if rad_to_deg((first[b] as Quaternion).angle_to(
					skel.get_bone_pose_rotation(b))) > 0.5:
				moved += 1

	var sounds: PackedStringArray = _importer.list_sounds(uri)

	print("\n%s" % uri.get_file())
	print("   %d sequences, playing '%s' (%.2f s, %d keys)" % [poses.size(), wanted, length, keys])
	print("   %d of %d bones move, largest %.1f degrees"
		% [moved, 0 if skel == null else skel.get_bone_count(), largest])
	if sounds.size() > 0:
		print("   sounds: %s" % str(sounds[0]).get_file())

	node.queue_free()
	return {"poses": poses.size(), "moved": moved, "sounds": sounds.size()}
