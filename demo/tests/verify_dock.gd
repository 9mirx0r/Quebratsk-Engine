extends Node

## Drives the editor dock's logic without the editor GUI.
##
## The dock is where a non-Godot user spends all their time, so "it compiles" is not
## evidence it works. This walks the actual flow — detect a game, add it, search, read the
## pose list, restart — and prints what each step really produced, including the exact
## wording the user would see.

const DockScript := preload("res://addons/quebratsk_editor/quebratsk_dock.gd")
const GMOD := "C:/Program Files (x86)/Steam/steamapps/common/GarrysMod/garrysmod"
const HL2 := "C:/Program Files (x86)/Steam/steamapps/common/half-life 2/hl2"

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
	print("\n2. Adding two games")
	_dock._add_game_folder(GMOD, "Garry's Mod")
	print("   says: %s" % _dock._status.text)
	# A second game is what makes the origin column matter: both ship a police.mdl.
	_dock._add_game_folder(HL2, "Half-Life 2")
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


## Select a category by its English label.
##
## Matched against CATEGORIES rather than the dropdown text: the dropdown is translated,
## so on a Spanish editor "Characters & people" reads "Personajes y personas" and matching
## on what is displayed makes this test pass or fail by locale. The index is what the dock
## actually uses, and it is locale-independent.
func _pick_category(label: String) -> void:
	for i in _dock.CATEGORIES.size():
		if str((_dock.CATEGORIES[i] as Dictionary)["label"]) == label:
			_dock._filter.select(i)
			_dock._refresh_results()
			return
	push_error("no such category: %s" % label)


func _check_search() -> void:
	print("\n3. Ready-made categories, no search term")
	_dock._search.text = ""
	for c in _dock.CATEGORIES:
		var label := str((c as Dictionary)["label"])
		var t0 := Time.get_ticks_msec()
		_pick_category(label)
		var first: TreeItem = _dock._results.get_root().get_first_child()
		var sample := ""
		if first != null and first.is_selectable(0):
			sample = "%s %s" % [first.get_text(0), first.get_text(1)]
		print("   %-22s %-46s (%d ms)  e.g. %s"
			% [label, _dock._status.text, Time.get_ticks_msec() - t0, sample])

	print("\n   searching inside a category")
	_dock._search.text = "player"
	_pick_category("Characters & people")
	print("   'player' in Characters & people -> %s" % _dock._status.text)
	var it: TreeItem = _dock._results.get_root().get_first_child()
	var listed := 0
	while it != null and listed < 3:
		print("     %s   %s" % [it.get_text(0), it.get_text(1)])
		it = it.get_next()
		listed += 1
	_dock._search.text = ""


func _check_pick() -> void:
	print("\n4. Picking a model")
	# Companion files (.vvd/.vtx/.ani/.phy) are pieces of a model, not things a user
	# picks. Searching "police" in Everything must not list them.
	_dock._search.text = "police"
	_pick_category("Everything")
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
	_pick_category("Characters & people")

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

	# The preview is debounced, so drive it directly rather than waiting on a timer that
	# does not tick meaningfully in a headless run.
	var t0 := Time.get_ticks_msec()
	_dock._refresh_preview()
	var dt := Time.get_ticks_msec() - t0
	var node: Node3D = _dock._preview_node
	print("   preview: visible=%s node=%s in %d ms"
		% [_dock._preview_frame.visible, "null" if node == null else node.get_class(), dt])
	if node != null:
		print("     camera distance %.2f m, pivot offset %s"
			% [_dock._orbit_distance, _dock._preview_pivot.position.round()])

	# Pressing Add must adopt the previewed node, not import a second copy.
	var before: Node3D = _dock._preview_node
	_dock._plugin = null   # no EditorInterface headless; stop before the scene insert
	_dock._on_add_pressed()
	print("   after Add with no editor: preview kept = %s"
		% (_dock._preview_node == before))

	# A sound is listed but was inert until now: parse the WAV out of the archive and
	# report what came back, since a bad header yields silence rather than an error.
	_dock._search.text = ""
	_pick_category("Sounds")
	var snd: TreeItem = _dock._results.get_root().get_first_child()
	while snd != null:
		var uri := str(snd.get_metadata(0))
		if uri.ends_with(".wav"):
			var stream: AudioStreamWAV = _dock._load_wav(uri)
			if stream == null:
				print("   sound: %s -> unsupported" % uri.get_file())
			else:
				print("   sound: %s -> %d Hz, %s, %d KiB"
					% [uri.get_file(), stream.mix_rate,
					   "stereo" if stream.stereo else "mono", stream.data.size() / 1024])
			break
		snd = snd.get_next()

	# A texture is findable but not something you drop into a scene on its own.
	_dock._search.text = ""
	_pick_category("Textures & materials")
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
