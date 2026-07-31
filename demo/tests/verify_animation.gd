extends Node

## Does an imported animation actually move a bone?
##
## An AnimationPlayer that exists, holds an Animation of the right length and plays without
## error still proves nothing: if the track path resolves to no node, Godot plays it
## perfectly and the model stands still. So this seeks to several times and compares the
## bone poses it finds there.

const GMOD := "C:/Program Files (x86)/Steam/steamapps/common/GarrysMod/garrysmod"

var _vfs: VFSManager
var _importer: UnifiedAssetImporter


func _ready() -> void:
	_vfs = VFSManager.new()
	_importer = UnifiedAssetImporter.new()
	add_child(_vfs)
	add_child(_importer)
	_importer.set_vfs(_vfs)

	var scan: Dictionary = _vfs.scan_game_directory(GMOD)
	var n := 0
	for archive in scan.get("archives", []):
		_vfs.mount_container("gmod%d" % n, str(archive))
		n += 1

	print("\n=== ANIMATION VERIFICATION ===")
	var uri := _pick_model()
	if uri.is_empty():
		print("no player model found; nothing to verify")
		get_tree().quit()
		return
	print("model: %s" % uri)

	var wanted := _pick_sequence(uri)
	if wanted.is_empty():
		get_tree().quit()
		return

	_check(uri, wanted)
	print("\n=== DONE ===")
	get_tree().quit()


func _pick_model() -> String:
	var hit: Dictionary = _vfs.find_files("models/player/", PackedStringArray(["mdl"]),
		PackedStringArray(), 0)
	for entry in hit["files"]:
		var uri := str(entry)
		# A model whose geometry is not mounted cannot be checked for motion.
		if _vfs.file_exists(uri.replace(".mdl", ".vvd")):
			return uri
	return ""


## Prefer a sequence whose name suggests motion. A held pose would move no bone even if
## every part of the pipeline worked, so verifying against one proves nothing.
func _pick_sequence(uri: String) -> String:
	var poses: PackedStringArray = _importer.list_poses(uri)
	print("%d sequences available" % poses.size())
	for hint in ["walk", "run", "sprint", "reload", "idle"]:
		for p in poses:
			if str(p).to_lower().contains(hint):
				print("picked '%s' (matched '%s')" % [p, hint])
				return str(p)
	if poses.is_empty():
		print("model exposes no sequences")
		return ""
	print("picked '%s' (first available)" % poses[0])
	return str(poses[0])


func _check(uri: String, sequence: String) -> void:
	var t0 := Time.get_ticks_msec()
	var node: Node3D = _importer.load_model(uri, "", PackedStringArray([sequence]))
	var load_ms := Time.get_ticks_msec() - t0
	if node == null:
		print("load_model returned null, error code %d" % _importer.get_last_error_code())
		return
	add_child(node)
	print("loaded in %d ms -> %s" % [load_ms, node.get_class()])

	var skel := node as Skeleton3D
	if skel == null:
		print("root is not a Skeleton3D; nothing to animate")
		return

	var player: AnimationPlayer = node.get_node_or_null("AnimationPlayer")
	if player == null:
		print("NO AnimationPlayer was attached  ** FAILED **")
		return

	var names := player.get_animation_list()
	print("AnimationPlayer holds %d animation(s): %s" % [names.size(), ", ".join(names)])
	if names.is_empty():
		print("** FAILED **")
		return

	var anim: Animation = player.get_animation(names[0])
	print("  '%s': %.3f s, %d tracks, loop=%s"
		% [names[0], anim.get_length(), anim.get_track_count(),
		   anim.get_loop_mode() != Animation.LOOP_NONE])

	# The track path is the part most likely to be silently wrong, so read one back and
	# resolve it by hand before trusting the player.
	var path := anim.track_get_path(0)
	var resolved := player.get_node_or_null(player.root_node)
	print("  track 0 path '%s' -> root_node '%s' resolves to %s"
		% [path, player.root_node, "null" if resolved == null else resolved.get_class()])
	if resolved is Skeleton3D:
		var bone_name := str(path.get_subname(0)) if path.get_subname_count() > 0 else ""
		print("  subname '%s' -> bone index %d" % [bone_name, (resolved as Skeleton3D).find_bone(bone_name)])

	_sample(skel, player, str(names[0]), anim.get_length())
	_cross_check(uri, sequence, player, str(names[0]))
	_check_long_sequences(uri)


## Frame 0 of an animation and the static pose of the same sequence are two independent
## paths through the decoder, and they must land on the same skeleton. "Something moved"
## would be satisfied by garbage; agreeing with a result that was already verified is not.
func _cross_check(uri: String, sequence: String, player: AnimationPlayer, anim_name: String) -> void:
	var posed: Node3D = _importer.load_model(uri, sequence)
	if posed == null:
		print("\n  cross-check skipped: the static pose failed to load")
		return
	add_child(posed)
	var static_skel := posed as Skeleton3D

	player.seek(0.0, true)
	var animated := player.get_parent() as Skeleton3D

	var worst := 0.0
	var worst_bone := ""
	for b in static_skel.get_bone_count():
		var name := static_skel.get_bone_name(b)
		var other := animated.find_bone(name)
		if other < 0:
			continue
		var delta := rad_to_deg(static_skel.get_bone_pose_rotation(b).angle_to(
			animated.get_bone_pose_rotation(other)))
		if delta > worst:
			worst = delta
			worst_bone = name

	print("\n  frame 0 of '%s' against the static pose of the same sequence:" % anim_name)
	print("    worst disagreement: %s, %.2f degrees" % [worst_bone, worst])
	if worst > 1.0:
		print("    ** the two paths disagree **")
	else:
		print("    the animation starts where the pose stands")
	posed.queue_free()


## Long sequences are stored in sections, each with its own track chain, so any frame past
## the first section exercises arithmetic that a short animation never reaches.
func _check_long_sequences(uri: String) -> void:
	var poses: PackedStringArray = _importer.list_poses(uri)
	var tried := 0
	print("\n  sectioned sequences (long ones), which use a different code path:")
	for p in poses:
		if tried >= 3:
			break
		var name := str(p)
		var node: Node3D = _importer.load_model(uri, "", PackedStringArray([name]))
		if node == null:
			continue
		var ap: AnimationPlayer = node.get_node_or_null("AnimationPlayer")
		if ap == null or ap.get_animation_list().is_empty():
			node.queue_free()
			continue
		var a: Animation = ap.get_animation(ap.get_animation_list()[0])
		# Under a second is a single section; nothing new is exercised.
		if a.get_length() < 2.0:
			node.queue_free()
			continue

		add_child(node)
		var skel := node as Skeleton3D
		ap.play(name)
		var seen := {}
		for frac in [0.0, 0.4, 0.8, 0.99]:
			ap.seek(a.get_length() * frac, true)
			seen[frac] = skel.get_bone_pose_rotation(0)
		var spread := 0.0
		for frac in seen:
			spread = maxf(spread, rad_to_deg(seen[0.0].angle_to(seen[frac])))

		# A track that never changes is stored as one key, so a low count is not by itself
		# a truncated decode. What would be is the richest track ending before the
		# animation does — the parser reports a partial decode on stderr, and the length
		# it publishes is the part it actually read.
		var expected := int(round(a.get_length() / a.step)) + 1
		var richest := 0
		var last_key := 0.0
		for i in a.get_track_count():
			var count := a.track_get_key_count(i)
			richest = maxi(richest, count)
			if count > 1:
				last_key = maxf(last_key, a.track_get_key_time(i, count - 1))
		# One key on every track means nothing in the sequence moves — a reference or
		# ragdoll pose, which is a held stance and not a failed decode.
		var verdict := ""
		if richest <= 1:
			verdict = "   (a held pose, nothing animates)"
		elif not is_equal_approx(last_key, a.get_length()):
			verdict = "   ** ENDS EARLY **"
		print("    %-28s %.2f s, %d tracks, root spans %.1f deg, %d keys, last at %.2f s%s"
			% [name, a.get_length(), a.get_track_count(), spread, richest, last_key, verdict])
		node.queue_free()
		tried += 1
	if tried == 0:
		print("    none found on this model")


## Seek to several points and report how far each bone moved. Motion is the whole claim.
func _sample(skel: Skeleton3D, player: AnimationPlayer, name: String, length: float) -> void:
	player.play(name)

	var times := [0.0, length * 0.25, length * 0.5, length * 0.75]
	var snapshots := []
	for t in times:
		player.seek(float(t), true)
		var frame := []
		for b in skel.get_bone_count():
			frame.append(skel.get_bone_pose_rotation(b))
		snapshots.append(frame)

	print("\n  bone movement across the sequence:")
	var moved := 0
	var largest := 0.0
	var largest_bone := ""
	for b in skel.get_bone_count():
		var max_delta := 0.0
		for i in range(1, snapshots.size()):
			var a: Quaternion = snapshots[0][b]
			var c: Quaternion = snapshots[i][b]
			max_delta = maxf(max_delta, rad_to_deg(a.angle_to(c)))
		if max_delta > 0.5:
			moved += 1
		if max_delta > largest:
			largest = max_delta
			largest_bone = skel.get_bone_name(b)

	print("    %d of %d bones rotate by more than half a degree" % [moved, skel.get_bone_count()])
	print("    largest: %s, %.1f degrees" % [largest_bone, largest])
	if moved == 0:
		print("    ** FAILED: the animation plays and nothing moves **")
	else:
		print("    animation drives the skeleton")
