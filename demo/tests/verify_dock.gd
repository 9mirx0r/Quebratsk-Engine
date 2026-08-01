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

## Four games in one folder: Half-Life, Counter-Strike, Condition Zero and its Deleted
## Scenes. This is how Steam lays GoldSrc out, and it is the case the dock used to get
## wrong, offering all four at once under a single name.
const GOLDSRC := "C:/Program Files (x86)/Steam/steamapps/common/Half-Life"

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
	_check_game_filter()
	_check_animation_scope()
	_check_map_description()
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
	print("\n2. Adding the games")
	# Adding a game is the one operation a user waits on, and it also runs on every editor
	# start when the previous session's games are restored. Worth knowing what it costs.
	var t0 := Time.get_ticks_msec()
	_dock._add_game_folder(GMOD, "Garry's Mod")
	var gmod_ms := Time.get_ticks_msec() - t0
	print("   says: %s   (%d ms)" % [_dock._status.text, gmod_ms])
	# A second game is what makes the origin column matter: both ship a police.mdl.
	t0 = Time.get_ticks_msec()
	_dock._add_game_folder(HL2, "Half-Life 2")
	print("   says: %s   (%d ms)" % [_dock._status.text, Time.get_ticks_msec() - t0])

	# One folder, four games. Whether they are offered separately is the question.
	if DirAccess.dir_exists_absolute(GOLDSRC):
		t0 = Time.get_ticks_msec()
		_dock._add_game_folder(GOLDSRC, "Half-Life")
		print("   says: %s   (%d ms)" % [_dock._status.text, Time.get_ticks_msec() - t0])

	_split_mount_cost()


## Where the time in "add a game" actually goes.
##
## Adding a game is three different jobs wearing one button: a recursive walk of the folder
## to find out what is in it, one mount per archive, and a second recursive walk to index
## the loose files. Guessing which one dominates has been wrong twice, so this measures.
func _split_mount_cost() -> void:
	var vfs := VFSManager.new()
	add_child(vfs)

	var t := Time.get_ticks_msec()
	var scan: Dictionary = vfs.scan_game_directory(GMOD)
	print("     scan_game_directory   %4d ms  (%d archives found)"
		% [Time.get_ticks_msec() - t, int(scan.get("total_archives", 0))])

	var n := 0
	for archive in scan.get("archives", []):
		t = Time.get_ticks_msec()
		vfs.mount_container("probe%d" % n, str(archive))
		print("     mount_container       %4d ms  %s"
			% [Time.get_ticks_msec() - t, str(archive).get_file()])
		n += 1

	t = Time.get_ticks_msec()
	var loose: int = vfs.mount_directory("probe_loose", GMOD)
	print("     mount_directory       %4d ms  (%d loose files)"
		% [Time.get_ticks_msec() - t, loose])

	vfs.queue_free()

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
		"player", PackedStringArray(wanted), PackedStringArray(drop),
		PackedStringArray(), 400)
	var new_us := Time.get_ticks_usec() - t0

	print("   filtering %d entries down to one page:" % all.size())
	print("     list_files + GDScript loop: %6.1f ms -> %d of %d"
		% [old_us / 1000.0, old_hits.size(), old_total])
	print("     find_files in the engine:   %6.1f ms -> %d of %d"
		% [new_us / 1000.0, (found["files"] as PackedStringArray).size(), int(found["total"])])


## Does the game dropdown offer the games, and does picking one narrow the search to it?
##
## Steam calls one folder "Half-Life" and there are four games inside it. The dropdown used
## to list the mount, so asking for Half-Life handed back Counter-Strike and Condition Zero
## as well and there was no way to say which one you meant.
func _check_game_filter() -> void:
	print("
3b. the game dropdown")

	var labels := []
	for i in _dock._game_filter.item_count:
		labels.append(_dock._game_filter.get_item_text(i))
	print("   offers: %s" % ", ".join(PackedStringArray(labels)))

	# Every entry past "All games" has to narrow the result, and to content that really is
	# that game's. A filter that returns the same list as no filter is not a filter.
	# "Everything", not category 0. The first two are Favourites and Recently imported,
	# which are curated lists that never run a search at all: reading a total after selecting
	# one reads whatever the previous search left behind, which is how this check first
	# reported that nothing narrowed anything.
	_dock._search.text = ""
	for i in _dock.CATEGORIES.size():
		if str((_dock.CATEGORIES[i] as Dictionary)["label"]) == "Everything":
			_dock._filter.select(i)
			break
	_dock._refresh_results()
	var everything: int = _dock._last_total

	var checked := 0
	var narrowed := 0
	var clean := 0
	for i in range(1, _dock._game_filter.item_count):
		if checked >= 5:
			break
		_dock._game_filter.select(i)
		_dock._refresh_results()
		checked += 1
		var total: int = _dock._last_total
		if total < everything and total > 0:
			narrowed += 1

		# Read a page back and ask the engine which game each hit belongs to.
		var meta = _dock._game_filter.get_item_metadata(i)
		var wanted := "" if not (meta is Dictionary) else str((meta as Dictionary).get("game", ""))
		var stray := 0
		var row: TreeItem = _dock._results.get_root().get_first_child()
		var looked := 0
		while row != null and looked < 40:
			var uri := str(row.get_metadata(0))
			if not wanted.is_empty() and not uri.is_empty():
				if _dock._vfs.get_game_of(uri) != wanted:
					stray += 1
			row = row.get_next()
			looked += 1
		if stray == 0:
			clean += 1
		print("   %-32s %6d of %d files, %d from another game"
			% [_dock._game_filter.get_item_text(i), total, everything, stray])

	_dock._game_filter.select(0)
	_dock._refresh_results()
	print("   %d of %d narrow the search, %d return only their own game"
		% [narrowed, checked, clean])
	if checked > 0 and clean < checked:
		print("   ** the filter leaks between games **")


## Does "the usual moves" actually bring in more than one?
##
## The dock could import one sequence, so a character arrived able to stand still or able to
## walk, never both. The engine could always do more; nothing in the dock asked it to.
func _check_animation_scope() -> void:
	print("
3c. bringing in a set of animations")

	_dock._search.text = "player"
	_dock._refresh_results()
	var row: TreeItem = _dock._results.get_root().get_first_child()
	var uri := ""
	while row != null:
		var candidate := str(row.get_metadata(0))
		if candidate.ends_with(".mdl") and _dock._importer.list_poses(candidate).size() >= 20:
			uri = candidate
			break
		row = row.get_next()
	if uri.is_empty():
		print("   no model with enough sequences among the results")
		return

	# Through the real path: the pose list is what the single-sequence choice draws on, and
	# setting the URI alone leaves it empty, which reads as the choice returning nothing.
	_dock._selected_uri = uri
	_dock._load_poses(uri)
	_dock._animate_toggle.button_pressed = true

	for scope in [_dock.SCOPE_ONE, _dock.SCOPE_USUAL]:
		for i in _dock._animate_scope.item_count:
			if str(_dock._animate_scope.get_item_metadata(i)) == scope:
				_dock._animate_scope.select(i)
		# The single-sequence choice needs a pose named; the set finds its own.
		if scope == _dock.SCOPE_ONE and _dock._pose_picker.item_count > 1:
			_dock._pose_picker.select(1)
		var wanted: PackedStringArray = _dock._chosen_animations()
		print("   %-24s asks for %d sequence(s): %s"
			% [scope, wanted.size(), ", ".join(wanted)])

	_dock._animate_toggle.button_pressed = false


## Does picking a level say what is in it?
##
## A .bsp used to show a filename and a size, which tells you nothing about whether you are
## looking at a bomb site or a corridor. The map's own entity list is the only description of
## it that exists and it was being read and discarded.
func _check_map_description() -> void:
	print("
3d. what a level says it contains")

	var hit: Dictionary = _dock._vfs.find_files("maps/", PackedStringArray(["bsp"]),
		PackedStringArray(), PackedStringArray(), 40)
	var uri := ""
	for entry in hit["files"]:
		if not _dock._importer.load_map_entities(str(entry)).is_empty():
			uri = str(entry)
			break
	if uri.is_empty():
		print("   no readable map among the results")
		return

	# Through the real path, so this measures what a person selecting a map would see.
	_dock._selected_uri = uri
	_dock._picked_kind.text = "Level"
	_dock._describe_map(uri)

	var said: String = _dock._picked_kind.text
	print("   %s" % uri.get_file())
	for line in said.split("
"):
		print("      %s" % line)
	if said.contains("
"):
		print("   the level describes itself")
	else:
		print("   ** it said nothing beyond its type **")


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
	_check_order()


## The list claims to show "the first 400 of 41,969", which is only true if there is a first.
## The index is a hash map, so before this was ordered the page was whatever the bucket
## layout gave, and it changed whenever the map was resized.
func _check_order() -> void:
	_dock._search.text = ""
	_pick_category("Everything")

	var previous := ""
	var out_of_order := 0
	var rows := 0
	var row: TreeItem = _dock._results.get_root().get_first_child()
	while row != null:
		var name := str(row.get_metadata(0)).get_file()
		if not previous.is_empty() and name < previous:
			out_of_order += 1
		previous = name
		rows += 1
		row = row.get_next()

	print("\n   %d rows, %d out of order" % [rows, out_of_order])

	# Same query twice must give the same page, which is the property that was missing.
	var first: Dictionary = _dock._vfs.find_files("police", PackedStringArray(["mdl"]),
		PackedStringArray(), PackedStringArray(), 5)
	var again: Dictionary = _dock._vfs.find_files("police", PackedStringArray(["mdl"]),
		PackedStringArray(), PackedStringArray(), 5)
	print("   'police' first five: %s" % ", ".join(first["files"]))
	print("   repeatable: %s" % (first["files"] == again["files"]))

	# Searching one game instead of all of them. With two games mounted and both shipping a
	# police.mdl, the count going down and the origins collapsing to one name is the whole
	# point of the control.
	var all_games: Dictionary = _dock._vfs.find_files("police", PackedStringArray(["mdl"]),
		PackedStringArray(), PackedStringArray(), 0)
	for i in _dock._game_filter.item_count:
		_dock._game_filter.select(i)
		var label: String = _dock._game_filter.get_item_text(i)
		var scope: Dictionary = _dock._chosen_scope()
		var only: Dictionary = _dock._vfs.find_files("police", PackedStringArray(["mdl"]),
			PackedStringArray(), scope["prefixes"], 0, scope["games"])
		var origins := {}
		for f in only["files"]:
			origins[_dock._game_of(str(f))] = true
		print("   'police' in %-14s -> %3d hits, from: %s"
			% [label, int(only["total"]), ", ".join(PackedStringArray(origins.keys()))])
	_dock._game_filter.select(0)
	print("   (all games together: %d)" % int(all_games["total"]))

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

	# Sounds were listed and playable but could not be placed or kept. Check one of each
	# format the dock claims: a bad header yields silence rather than an error, so "it
	# returned a stream" is the least that has to be shown.
	_dock._search.text = ""
	_pick_category("Sounds")
	# Which formats these games actually ship decides which paths this run can prove.
	for ext in _dock.SOUND_EXTENSIONS:
		var found: Dictionary = _dock._vfs.find_files("", PackedStringArray([ext]),
			PackedStringArray(), PackedStringArray(), 1)
		print("   %-4s in the mounted games: %d" % [ext, int(found["total"])])
	var seen := {}
	var snd: TreeItem = _dock._results.get_root().get_first_child()
	while snd != null:
		var uri := str(snd.get_metadata(0))
		var ext := uri.get_extension().to_lower()
		if _dock.SOUND_EXTENSIONS.has(ext) and not seen.has(ext):
			seen[ext] = true
			var stream: AudioStream = _dock._load_sound(uri)
			if stream == null:
				print("   sound %-4s %s -> unsupported" % [ext, uri.get_file()])
			else:
				print("   sound %-4s %s -> %s, %.2f s"
					% [ext, uri.get_file(), stream.get_class(), stream.get_length()])
				var player: Node3D = _dock._build_sound_player(uri)
				print("            add to scene -> %s named '%s'"
					% ["nothing" if player == null else player.get_class(),
					   "" if player == null else player.name])
				if player != null:
					player.queue_free()
		snd = snd.get_next()

	# Saving a sound writes the audio file itself rather than a scene wrapping it.
	_dock._search.text = ""
	_pick_category("Sounds")
	var to_save: TreeItem = _dock._results.get_root().get_first_child()
	if to_save != null and to_save.is_selectable(0):
		DirAccess.make_dir_recursive_absolute(_dock.SAVE_DIR)
		var written: String = _dock._save_sound(str(to_save.get_metadata(0)))
		if written.is_empty():
			print("   saving a sound wrote nothing  ** FAILED **")
		else:
			var check := FileAccess.open(written, FileAccess.READ)
			print("   saved %s (%d bytes on disk)"
				% [written, 0 if check == null else check.get_length()])
			if check != null:
				check.close()
			DirAccess.remove_absolute(ProjectSettings.globalize_path(written))

	# A texture is findable but not something you drop into a scene on its own.
	_dock._search.text = ""
	_pick_category("Textures & materials")
	# The first row of this category is usually a .vmt, which is a material and has no image
	# of its own. Walk to something that is actually a picture.
	var tex: TreeItem = _dock._results.get_root().get_first_child()
	while tex != null and not str(tex.get_metadata(0)).get_extension().to_lower() in ["vtf", "png", "tga", "paa"]:
		tex = tex.get_next()
	if tex != null and tex.is_selectable(0):
		_dock._results.set_selected(tex, 0)
		_dock._on_result_selected()
		print("   texture picked -> Add enabled: %s" % (not _dock._add_button.disabled))

		# Whether the preview actually draws. This check did not exist, and the preview had
		# never worked: load_texture() could not resolve a full vfs:// URI, so it returned
		# null and the frame stayed blank without a word to anyone.
		var drawn: bool = _dock._preview_texture(str(tex.get_metadata(0)))
		var shown: Texture2D = _dock._texture_frame.texture
		print("   texture preview -> %s" % [
			"nothing drawn  ** FAILED **" if not drawn or shown == null
			else "%dx%d" % [shown.get_width(), shown.get_height()]])
		print("   says: %s" % _dock._status.text)


func _check_persistence() -> void:
	print("\n5. Reopening the editor")
	var t0 := Time.get_ticks_msec()
	var fresh: VBoxContainer = DockScript.new()
	add_child(fresh)
	print("   the dock was ready again in %d ms" % (Time.get_ticks_msec() - t0))
	print("   restored %d game(s): %s" % [fresh._sources.size(), fresh._status.text])
	var row: TreeItem = fresh._games.get_root().get_first_child()
	while row != null:
		print("     row: %s" % row.get_text(0))
		row = row.get_next()
	fresh.queue_free()
