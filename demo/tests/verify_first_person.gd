extends Node

## Would a camera on the head bone actually be at eye level?
##
## The first-person view is built by putting the camera where the model's own head is, so that
## looking down shows the body that is really standing there. Nothing about that can be judged
## from a screenshot alone, and the ways it goes wrong are quiet: a head bone that was never
## found leaves the view at a fixed height, a bone matched by name alone can be a "HeadNub"
## marker floating somewhere, and a head that does not move with the animation gives a view
## welded in place on a body that walks out from under it.
##
## So this measures the three things the screenshot cannot show: where the bone is relative to
## the model, whether it moves, and whether the hand is an arm's length from it.

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

	print("\n=== FIRST PERSON BODY ===")

	var hit: Dictionary = _vfs.find_files("models/player/", PackedStringArray(["mdl"]),
		PackedStringArray(["vvd", "vtx", "ani", "phy"]), PackedStringArray(), 0)

	var checked := 0
	var with_head := 0
	var eye_level := 0
	var moving := 0
	var with_hand := 0

	for entry in hit["files"]:
		if checked >= 8:
			break
		var result: Dictionary = _check(str(entry))
		if result.is_empty():
			continue
		checked += 1
		if result["head"]:
			with_head += 1
		if result["eye_level"]:
			eye_level += 1
		if result["moves"]:
			moving += 1
		if result["hand"]:
			with_hand += 1

	print("\n=== SUMMARY ===")
	print("  %d character models checked" % checked)
	print("  %d have a head bone that can be found by name" % with_head)
	print("  %d put that bone at eye level rather than somewhere else" % eye_level)
	print("  %d move the head when the animation plays" % moving)
	print("  %d have a right hand bone to hang a weapon on" % with_hand)
	if checked > 0 and eye_level < with_head:
		print("  ** a bone matched by name is not always the head **")
	get_tree().quit()


## The same rule the sandbox uses, so this measures what the sandbox will actually do.
func _bone_like(skeleton: Skeleton3D, needles: Array) -> int:
	if skeleton == null:
		return -1
	var best := -1
	var best_length := 0
	for b in skeleton.get_bone_count():
		var name := skeleton.get_bone_name(b).to_lower()
		for needle in needles:
			if name.contains(str(needle)):
				if best < 0 or name.length() < best_length:
					best = b
					best_length = name.length()
				break
	return best


func _check(uri: String) -> Dictionary:
	var poses: PackedStringArray = _importer.list_poses(uri)
	if poses.size() < 20:
		return {}

	# A stance that moves, so a head welded in place shows up as one.
	var wanted := str(poses[0])
	for hint in ["walk", "run", "idle"]:
		for p in poses:
			if str(p).to_lower().begins_with(hint):
				wanted = str(p)
				break
		if wanted != str(poses[0]):
			break

	var body: Node3D = _importer.load_character(uri, "", PackedStringArray([wanted]))
	if body == null:
		return {}
	add_child(body)

	var skeleton: Skeleton3D = null
	for child in body.get_children():
		if child is Skeleton3D:
			skeleton = child as Skeleton3D
	if skeleton == null:
		body.queue_free()
		return {}

	var head := _bone_like(skeleton, ["head"])
	var hand := _bone_like(skeleton, ["r_hand", "r hand", "righthand", "hand_r"])

	# How tall the model is, taken from the bones rather than from a mesh AABB: the AABB of a
	# skinned mesh in Godot is the rest-pose bound, which is not where the bones are now.
	var lowest := INF
	var highest := -INF
	for b in skeleton.get_bone_count():
		var y: float = skeleton.get_bone_global_pose(b).origin.y
		lowest = minf(lowest, y)
		highest = maxf(highest, y)
	var height: float = highest - lowest

	var at_eye_level := false
	var head_y := 0.0
	if head >= 0 and height > 0.5:
		head_y = skeleton.get_bone_global_pose(head).origin.y
		# The head sits in the top fifth of a standing figure. Anything lower is a different
		# bone that happens to have "head" in its name.
		at_eye_level = (head_y - lowest) / height > 0.78

	# Does the head go anywhere while the animation plays? A view that does not follow is a
	# view that drifts off the body the moment it walks.
	var head_travel := 0.0
	var player: AnimationPlayer = skeleton.get_node_or_null("AnimationPlayer")
	if player == null:
		player = body.get_node_or_null("AnimationPlayer")
	if head >= 0 and player != null and not player.get_animation_list().is_empty():
		var name := str(player.get_animation_list()[0])
		var anim: Animation = player.get_animation(name)
		player.play(name)
		player.seek(0.0, true)
		var first: Vector3 = skeleton.get_bone_global_pose(head).origin
		for frac in [0.3, 0.6, 0.9]:
			player.seek(anim.get_length() * frac, true)
			head_travel = maxf(head_travel,
				first.distance_to(skeleton.get_bone_global_pose(head).origin))

	# An arm's length. A hand bone that reports as being at the head, or a metre and a half
	# away, is not the hand.
	var reach := 0.0
	if head >= 0 and hand >= 0:
		reach = skeleton.get_bone_global_pose(head).origin.distance_to(
			skeleton.get_bone_global_pose(hand).origin)

	print("\n%s" % uri.get_file())
	print("   %d bones, %.2f m tall, playing '%s'" % [skeleton.get_bone_count(), height, wanted])
	if head < 0:
		print("   no head bone found")
	else:
		print("   head '%s' at %.0f%% of height%s"
			% [skeleton.get_bone_name(head), 100.0 * (head_y - lowest) / maxf(height, 0.001),
			   "" if at_eye_level else "   ** not where a head goes **"])
		print("   head travels %.3f m over the sequence%s"
			% [head_travel, "" if head_travel > 0.005 else "   ** it does not move **"])
	if hand < 0:
		print("   no right hand bone found, the weapon stays on the camera")
	else:
		print("   hand '%s', %.2f m from the head" % [skeleton.get_bone_name(hand), reach])

	body.queue_free()
	return {
		"head": head >= 0,
		"eye_level": at_eye_level,
		"moves": head_travel > 0.005,
		"hand": hand >= 0 and reach > 0.15 and reach < 1.0,
	}
