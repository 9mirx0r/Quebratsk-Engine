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
	# Nothing is mounted yet, so the results list must be offering the first step rather
	# than reading as an empty search.
	var first: TreeItem = _dock._results.get_root().get_first_child()
	print("\n0. With no games added, the list says: %s"
		% ("nothing at all" if first == null else first.get_text(0)))

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


## Both ways of producing the same page of results, timed against each other in one run.
##
## The times printed above are a whole refresh, engine scan plus 400 Tree rows, so they
## cannot say which half the work is in. This does the filtering alone, twice: once the way
## the dock used to (copy the whole index out, sift it in GDScript) and once the way it does
## now. Same inputs, same output, so the difference is the approach and not the machine.
func _time_filtering() -> void:
	var drop: Array = _dock.COMPANION_EXTENSIONS
	var wanted := ["mdl"]

	var t0 := Time.get_ticks_usec()
	var all: PackedStringArray = _dock._vfs.list_files()
	var old_hits := PackedStringArray()
	var old_total := 0
	for uri in all:
		var ext := uri.get_extension().to_lower()
		if drop.has(ext) or not wanted.has(ext):
			continue
		if not uri.to_lower().contains("player"):
			continue
		old_total += 1
		if old_hits.size() < 400:
			old_hits.append(uri)
	var old_us := Time.get_ticks_usec() - t0

	t0 = Time.get_ticks_usec()
	var found: Dictionary = _dock._vfs.find_files(
		"player", PackedStringArray(wanted), PackedStringArray(drop), 400)
	var new_us := Time.get_ticks_usec() - t0

	print("   filtering %d entries down to one page:" % all.size())
	print("     list_files + GDScript loop: %6.1f ms -> %d of %d"
		% [old_us / 1000.0, old_hits.size(), old_total])
	print("     find_files in the engine:   %6.1f ms -> %d of %d"
		% [new_us / 1000.0, (found["files"] as PackedStringArray).size(), int(found["total"])])


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

	_time_filtering()

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

	# False positives are the risk here: reporting a missing .vvd for a model that
	# imported fine would send users hunting for a file they already have.
	var ok_node: Node3D = _dock._importer.load_model(_dock._selected_uri, "")
	var missing: PackedStringArray = _dock._importer.get_last_missing_companions()
	print("   after a SUCCESSFUL model load, missing companions: %s"
		% ("none" if missing.is_empty() else ", ".join(missing)))
	if ok_node != null:
		ok_node.queue_free()

	# A GoldSrc model needs no .vvd at all, so it must not be reported either.
	_dock._search.text = "gsg9"
	_pick_category("All models")
	var gs: TreeItem = _dock._results.get_root().get_first_child()
	if gs != null and gs.is_selectable(0):
		var n2: Node3D = _dock._importer.load_model(str(gs.get_metadata(0)), "")
		print("   GoldSrc-era model %s -> missing: %s"
			% [str(gs.get_metadata(0)).get_file(),
			   "none" if _dock._importer.get_last_missing_companions().is_empty() else "REPORTED"])
		if n2 != null:
			n2.queue_free()

	# Re-select the model. Every _pick_category() above cleared the selection, so without
	# this the preview check below runs on an empty URI and reports "node=null" for a
	# reason that has nothing to do with the preview.
	_dock._search.text = "police"
	_pick_category("Characters & people")
	var again: TreeItem = _dock._results.get_root().get_first_child()
	_dock._results.set_selected(again, 0)
	_dock._on_result_selected()

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

	# "Bring it in moving" is the only way an animation is reachable without writing code,
	# so it is worth driving the same way a person would: pick a pose, tick the box, look
	# at what comes out.
	_dock._search.text = "police"
	_pick_category("Characters & people")
	var pick: TreeItem = _dock._results.get_root().get_first_child()
	if pick != null and pick.is_selectable(0):
		_dock._results.set_selected(pick, 0)
		_dock._on_result_selected()
		var walk := -1
		for i in _dock._pose_picker.item_count:
			if _dock._pose_picker.get_item_text(i).to_lower().contains("walk"):
				walk = i
				break
		if walk >= 0:
			_dock._pose_picker.select(walk)
			_dock._animate_toggle.button_pressed = true
			_dock._refresh_preview()
			var previewed: Node3D = _dock._preview_node
			var ap: AnimationPlayer = null
			if previewed != null:
				ap = previewed.get_node_or_null("AnimationPlayer")
			print("   'Bring it in moving' with pose '%s' -> %s"
				% [_dock._pose_picker.get_item_text(walk),
				   "no AnimationPlayer" if ap == null
				   else "%s, %.2f s" % [", ".join(ap.get_animation_list()),
										ap.get_animation(ap.get_animation_list()[0]).get_length()]])
			# Saving is a second path to the same node, and it used to ignore both the pose
			# and the animation: it rebuilt the model with neither.
			var saved: Node3D = _dock._build_for_scene(_dock._selected_uri, true)
			var saved_ap: AnimationPlayer = null
			if saved != null:
				saved_ap = saved.get_node_or_null("AnimationPlayer")
			print("   the same pick saved as a scene -> %s"
				% ["no AnimationPlayer" if saved_ap == null
				   else ", ".join(saved_ap.get_animation_list())])
			if saved != null:
				saved.queue_free()
			_dock._animate_toggle.button_pressed = false

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
				print("   sound: %s -> %d Hz, %s, %.1f KiB"
					% [uri.get_file(), stream.mix_rate,
					   "stereo" if stream.stereo else "mono", stream.data.size() / 1024.0])
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
