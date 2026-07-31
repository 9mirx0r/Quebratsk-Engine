extends Node

## Drives the editor dock's logic without the editor GUI.
##
## The dock is where a non-Godot user spends all their time, so "it compiles" is not
## evidence it works. This walks the actual flow — detect a game, add it, search, read the
## pose list, restart — and prints what each step really produced, including the exact
## wording the user would see.

const DockScript := preload("res://addons/quebratsk_editor/quebratsk_dock.gd")
const GMOD := "C:/Program Files (x86)/Steam/steamapps/common/GarrysMod/garrysmod"

var _dock: VBoxContainer


func _ready() -> void:
	# Start clean: the dock restores last session's games from user://.
	DirAccess.remove_absolute(ProjectSettings.globalize_path(DockScript.MOUNTS_FILE))

	_dock = DockScript.new()
	add_child(_dock)
	await get_tree().process_frame

	print("\n=== DOCK VERIFICATION ===")
	_check_detect()
	_check_add()
	_check_search()
	_check_pick()
	_check_persistence()
	print("\n=== DONE ===")
	get_tree().quit()


func _check_detect() -> void:
	print("\n1. Games offered in the Add menu")
	var games: Dictionary = SteamLibraryDetector.detect_installed_games()
	print("   %d found" % games.size())
	for key in games:
		print("     - %s" % key)


func _check_add() -> void:
	print("\n2. Adding Garry's Mod")
	_dock._add_game_folder(GMOD, "Garry's Mod")
	print("   says: %s" % _dock._status.text)

	# The point of the redesign: one row per game, not one per archive file.
	var row: TreeItem = _dock._games.get_root().get_first_child()
	var rows := 0
	while row != null:
		print("     row: %s" % row.get_text(0))
		row = row.get_next()
		rows += 1
	print("   %d row(s) for %d internal mount(s)"
		% [rows, _dock._vfs.get_mounts_info().size()])


func _check_search() -> void:
	print("\n3. Searching")
	_dock._search.text = "player"
	_dock._filter.select(1) # Characters & props
	_dock._refresh_results()
	print("   'player' in Characters & props -> %s" % _dock._status.text)

	var first: TreeItem = _dock._results.get_root().get_first_child()
	var listed := 0
	while first != null and listed < 3:
		print("     %s" % first.get_text(0))
		first = first.get_next()
		listed += 1

	_dock._search.text = ""
	_dock._filter.select(0)
	var t0 := Time.get_ticks_msec()
	_dock._refresh_results()
	print("   unfiltered refresh %d ms -> %s"
		% [Time.get_ticks_msec() - t0, _dock._status.text])


func _check_pick() -> void:
	print("\n4. Picking a model")
	# Companion files (.vvd/.vtx/.ani/.phy) are pieces of a model, not things a user
	# picks. Searching "police" in Everything must not list them.
	_dock._search.text = "police"
	_dock._filter.select(0)
	_dock._refresh_results()
	var scan: TreeItem = _dock._results.get_root().get_first_child()
	var companions := 0
	var total := 0
	while scan != null:
		var ext := str(scan.get_text(0)).get_extension().to_lower()
		if ext in ["vvd", "vtx", "ani", "phy"]:
			companions += 1
		total += 1
		scan = scan.get_next()
	print("   'police' in Everything -> %d rows, %d of them companion files" % [total, companions])

	_dock._search.text = "police"
	_dock._filter.select(1)
	_dock._refresh_results()

	var item: TreeItem = _dock._results.get_root().get_first_child()
	if item == null:
		print("   nothing to pick")
		return
	_dock._results.set_selected(item, 0)
	_dock._on_result_selected()

	print("   name:  %s" % _dock._picked_name.text)
	print("   about: %s" % _dock._picked_kind.text)
	print("   says:  %s" % _dock._status.text)
	print("   pose dropdown visible: %s (%d entries, first option '%s')"
		% [_dock._pose_row.visible, _dock._pose_picker.item_count,
		   _dock._pose_picker.get_item_text(0) if _dock._pose_picker.item_count > 0 else ""])
	print("   Add button enabled: %s" % (not _dock._add_button.disabled))

	# A texture is findable but not something you drop into a scene on its own.
	_dock._search.text = ""
	_dock._filter.select(3) # Textures & materials
	_dock._refresh_results()
	var tex: TreeItem = _dock._results.get_root().get_first_child()
	if tex != null and tex.is_selectable(0):
		_dock._results.set_selected(tex, 0)
		_dock._on_result_selected()
		print("   texture picked -> Add enabled: %s" % (not _dock._add_button.disabled))
		print("   says: %s" % _dock._status.text)


func _check_persistence() -> void:
	print("\n5. Reopening the editor")
	var fresh: VBoxContainer = DockScript.new()
	add_child(fresh)
	print("   restored %d game(s): %s" % [fresh._sources.size(), fresh._status.text])
	var row: TreeItem = fresh._games.get_root().get_first_child()
	while row != null:
		print("     row: %s" % row.get_text(0))
		row = row.get_next()
	fresh.queue_free()
