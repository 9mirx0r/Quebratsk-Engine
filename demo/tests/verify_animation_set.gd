extends Node

## Can a model be brought in able to do more than one thing?
##
## A Condition Zero player model carries 494 sequences and the dock could import one, so a
## character arrived able to stand still or able to walk, never both. Anyone wanting idle,
## walk and run had to import the same model three times and reconcile three scenes.
##
## Picking the right few out of 494 is guesswork about names, and guesswork is worth checking:
## a rule that matches nothing leaves the model with one animation again, and a rule that
## matches too loosely gives it three names for the same movement, which looks like it worked.

const AnimationSets := preload("res://addons/quebratsk_editor/animation_sets.gd")

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

	print("\n=== ANIMATION SETS ===")

	var hit: Dictionary = _vfs.find_files("models/player/", PackedStringArray(["mdl"]),
		PackedStringArray(["vvd", "vtx", "ani", "phy"]), PackedStringArray(), 40)

	var checked := 0
	var roles_total := 0
	var played := 0
	var all_moved := 0

	for entry in hit["files"]:
		if checked >= 6:
			break
		var result: Dictionary = _check(str(entry))
		if result.is_empty():
			continue
		checked += 1
		roles_total += int(result["roles"])
		played += int(result["played"])
		if result["all_move"]:
			all_moved += 1

	print("\n=== SUMMARY ===")
	print("  %d models checked, %d roles recognised between them" % [checked, roles_total])
	print("  %d of those arrived as animations that play" % played)
	print("  %d of %d models have every imported animation moving a bone"
		% [all_moved, checked])
	if checked > 0 and played < roles_total:
		print("  ** a recognised sequence did not survive the import **")
	get_tree().quit()


func _check(uri: String) -> Dictionary:
	var poses: PackedStringArray = _importer.list_poses(uri)
	if poses.size() < 20:
		return {}

	var moves: Dictionary = AnimationSets.usual_moves(poses)
	if moves.is_empty():
		print("\n%s   %d sequences, none recognised" % [uri.get_file(), poses.size()])
		return {"roles": 0, "played": 0, "all_move": false}

	var wanted: PackedStringArray = AnimationSets.usual_move_names(poses)

	# No sequence may stand for two roles: one file imported under two names looks like a
	# model that can both walk and run when it can only do one of them.
	var unique := {}
	for w in wanted:
		unique[str(w)] = true

	var node: Node3D = _importer.load_model(uri, "", wanted)
	if node == null:
		return {}
	add_child(node)

	var skel := node as Skeleton3D
	var player: AnimationPlayer = node.get_node_or_null("AnimationPlayer")
	var names := PackedStringArray() if player == null else player.get_animation_list()

	# Every one of them has to actually move something. An animation that exists and holds
	# the model still is the failure this whole feature was meant to end.
	var moving := 0
	var details := []
	for name in names:
		var anim: Animation = player.get_animation(str(name))
		var richest := 0
		for t in anim.get_track_count():
			richest = maxi(richest, anim.track_get_key_count(t))
		if richest > 1:
			moving += 1
		details.append("%s(%d keys)" % [str(name), richest])

	print("\n%s   %d sequences" % [uri.get_file(), poses.size()])
	var roles := []
	for role in moves:
		roles.append("%s=%s" % [role, str(moves[role])])
	print("   recognised %d: %s" % [moves.size(), ", ".join(PackedStringArray(roles))])
	print("   imported %d, %d of them move%s"
		% [names.size(), moving,
		   "" if unique.size() == wanted.size() else "   ** a sequence was used twice **"])

	node.queue_free()
	return {
		"roles": wanted.size(),
		"played": names.size(),
		"all_move": names.size() > 0 and moving == names.size(),
	}
