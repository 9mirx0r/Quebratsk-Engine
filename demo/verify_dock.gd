extends Node

## Drives the editor dock's logic without the editor GUI.
##
## The dock is where a non-Godot user spends all their time, so "it compiles" is not
## evidence it works. This walks the actual flow — detect a game, mount it, search,
## read the pose list — and prints what each step really produced.

const DockScript := preload("res://addons/quebratsk_editor/quebratsk_dock.gd")
const GMOD := "C:/Program Files (x86)/Steam/steamapps/common/GarrysMod/garrysmod"

var _dock: VBoxContainer


func _ready() -> void:
	# Start from a clean slate: the dock restores last session's mounts from user://.
	DirAccess.remove_absolute(ProjectSettings.globalize_path(DockScript.MOUNTS_FILE))

	_dock = DockScript.new()
	add_child(_dock)
	await get_tree().process_frame

	print("\n=== DOCK VERIFICATION ===")
	_check_detect()
	_check_mount()
	_check_search()
	_check_poses()
	_check_persistence()
	print("\n=== DONE ===")
	get_tree().quit()


func _check_detect() -> void:
	print("\n1. Game detection (the first thing a new user clicks)")
	var games: Dictionary = SteamLibraryDetector.detect_installed_games()
	print("   %d installed games offered in the menu" % games.size())
	for key in games:
		print("     - %s" % key)


func _check_mount() -> void:
	print("\n2. Mounting Garry's Mod through the dock's own code path")
	_dock._mount_game_folder(GMOD, "Garry's Mod")
	print("   status line: %s" % _dock._status.text)

	var mounts: Array = _dock._vfs.get_mounts_info()
	var total := 0
	for m in mounts:
		total += int((m as Dictionary).get("file_count", 0))
	print("   %d sources mounted, %d files reachable" % [mounts.size(), total])

	# The mount rows the user actually sees.
	var row: TreeItem = _dock._mount_list.get_root().get_first_child()
	var shown := 0
	while row != null and shown < 3:
		print("     row: %s" % row.get_text(0))
		row = row.get_next()
		shown += 1


func _check_search() -> void:
	print("\n3. Searching")
	_dock._search.text = "player"
	_dock._filter.select(1) # Models
	_dock._refresh_results()
	print("   filter=Models search='player' -> %s" % _dock._status.text)

	var first: TreeItem = _dock._results.get_root().get_first_child()
	var listed := 0
	while first != null and listed < 3:
		print("     %s" % first.get_text(0))
		first = first.get_next()
		listed += 1

	# An empty search must not try to build a Tree of 100k rows.
	_dock._search.text = ""
	_dock._filter.select(0) # Everything
	var t0 := Time.get_ticks_msec()
	_dock._refresh_results()
	print("   unfiltered refresh took %d ms -> %s"
		% [Time.get_ticks_msec() - t0, _dock._status.text])


func _check_poses() -> void:
	print("\n4. Selecting a model populates the pose dropdown")
	_dock._search.text = "police"
	_dock._filter.select(1)
	_dock._refresh_results()

	var item: TreeItem = _dock._results.get_root().get_first_child()
	if item == null:
		print("   no model found to select")
		return
	_dock._results.set_selected(item, 0)
	_dock._on_result_selected()

	print("   selected: %s" % _dock._selected_uri)
	print("   detail:   %s" % _dock._detail_path.text.replace("\n", " | "))
	print("   pose row visible: %s, %d entries" % [_dock._pose_row.visible, _dock._pose_picker.item_count])
	if _dock._pose_picker.item_count > 1:
		print("   first real pose: %s" % _dock._pose_picker.get_item_text(1))
	print("   'Add to scene' enabled: %s" % (not _dock._add_to_scene.disabled))


func _check_persistence() -> void:
	print("\n5. Mounts survive an editor restart")
	var fresh: VBoxContainer = DockScript.new()
	add_child(fresh)
	var restored: Array = fresh._vfs.get_mounts_info()
	print("   a brand-new dock restored %d source(s): %s" % [restored.size(), fresh._status.text])
	fresh.queue_free()
