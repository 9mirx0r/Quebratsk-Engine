extends Node

## What sequences do Counter-Strike 1.6's own models actually carry?
##
## Every animation role in the sandbox is matched against a list of names somebody guessed at,
## and a guess that misses is invisible: the character simply stands still, which is exactly
## what it did. So before another hint is added to that list, this reads the names out of the
## game.
##
## Two questions: what a player model calls standing, walking, running, crouching and dying,
## and what a view model calls firing and reloading.

const REPORT := "user://cs16_sequences.txt"

var _vfs: VFSManager
var _importer: UnifiedAssetImporter
var _report := PackedStringArray()


func _say(line: String) -> void:
	print(line)
	_report.append(line)


func _ready() -> void:
	_vfs = VFSManager.new()
	_importer = UnifiedAssetImporter.new()
	add_child(_vfs); add_child(_importer); _importer.set_vfs(_vfs)

	# Counter-Strike 1.6 only. Steam keeps Half-Life, Counter-Strike, Condition Zero and its
	# Deleted Scenes under one folder called Half-Life, so mounting "the game" mounts four of
	# them and the answer would be about whichever happened to sort first.
	var root := "C:/Program Files (x86)/Steam/steamapps/common/Half-Life/cstrike"
	if not DirAccess.dir_exists_absolute(root):
		_say("cstrike is not installed at %s" % root)
		get_tree().quit()
		return
	var scan: Dictionary = _vfs.scan_game_directory(root)
	var n := 0
	for a in scan.get("archives", []):
		if _vfs.mount_container("cs%d" % n, str(a)): n += 1
	_vfs.mount_directory("cs_loose", root)

	_say("=== COUNTER-STRIKE 1.6 SEQUENCE NAMES ===")

	_dump("models/player/", 1, "player models")
	# Every one of them. A weapon whose reload sequence is spelled differently is a weapon
	# whose reload key does nothing, and there is no way to know which without looking at all
	# of them: the M3 reported no reload while the AUG reported one.
	_dump("models/v_", 99, "view models")

	var file := FileAccess.open(REPORT, FileAccess.WRITE)
	if file != null:
		file.store_string("\n".join(_report) + "\n")
		file.close()
		print("report written to %s" % ProjectSettings.globalize_path(REPORT))
	get_tree().quit()


func _dump(needle: String, how_many: int, what: String) -> void:
	_say("\n--- %s ---" % what)
	var hit: Dictionary = _vfs.find_files(needle, PackedStringArray(["mdl"]),
		PackedStringArray(["vvd", "vtx", "ani", "phy"]), PackedStringArray(), 40)
	var shown := 0
	for entry in hit["files"]:
		if shown >= how_many:
			return
		var uri := str(entry)
		var poses: PackedStringArray = _importer.list_poses(uri)
		if poses.is_empty():
			continue
		shown += 1
		_say("\n%s   %d sequences" % [uri.get_file(), poses.size()])
		# All of them for a view model, which has a few dozen; a sample for a player model,
		# which has hundreds and repeats the same roles once per weapon.
		var limit: int = poses.size()
		var row := PackedStringArray()
		for i in range(limit):
			row.append(str(poses[i]))
			if row.size() == 4:
				_say("   " + "  ".join(row))
				row.clear()
		if not row.is_empty():
			_say("   " + "  ".join(row))
		if poses.size() > limit:
			_say("   ... and %d more" % (poses.size() - limit))
