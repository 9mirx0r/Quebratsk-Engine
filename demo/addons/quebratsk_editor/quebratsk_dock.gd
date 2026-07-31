@tool
extends VBoxContainer

## The Quebratsk dock.
##
## Written for someone who has never opened Godot: pick a game they own, search for a
## thing by name, press Add. Three sections, in the order the work happens — your games,
## find something, what you picked.
##
## Vocabulary rule for everything user-facing in this file: say what the thing IS, not
## what the format calls it. A user who mounts Garry's Mod should see "Garry's Mod", not
## the three VPK filenames it happens to be split across; a .mdl is a "Model", not a
## "StudioMDL v49 asset". Technical detail belongs in tooltips, where it helps whoever
## goes looking for it and is invisible to everyone else.
##
## Engine calls follow the surface documented in docs/API.md.

const MOUNTS_FILE := "user://quebratsk_mounts.cfg"

## list_files() with no prefix returns every indexed entry — a Half-Life 2 plus Garry's
## Mod setup is over 100,000. Populating a Tree with that locks the editor.
const MAX_RESULTS := 400

## Extension -> what a person would call it, and the editor icon that reads as that thing.
const KINDS := {
	"mdl": {"name": "Model", "icon": "MeshInstance3D", "placeable": true},
	"p3d": {"name": "Model", "icon": "MeshInstance3D", "placeable": true},
	"bsp": {"name": "Map", "icon": "GridMap", "placeable": true},
	"wrp": {"name": "Terrain", "icon": "GridMap", "placeable": true},
	"vtf": {"name": "Texture", "icon": "ImageTexture", "placeable": false},
	"tga": {"name": "Texture", "icon": "ImageTexture", "placeable": false},
	"png": {"name": "Texture", "icon": "ImageTexture", "placeable": false},
	"paa": {"name": "Texture", "icon": "ImageTexture", "placeable": false},
	"vmt": {"name": "Material", "icon": "StandardMaterial3D", "placeable": false},
	"wav": {"name": "Sound", "icon": "AudioStreamPlayer", "placeable": false},
	"mp3": {"name": "Sound", "icon": "AudioStreamPlayer", "placeable": false},
	"ogg": {"name": "Sound", "icon": "AudioStreamPlayer", "placeable": false},
}

## Never listed. These are pieces of another file rather than things in their own right:
## a Source model is split across police.mdl + police.vvd + police.dx90.vtx + police.ani,
## and the importer pulls the companions in by itself. Showing them meant a search for
## "police" returned four rows where the user wanted one, three of which do nothing when
## picked.
const COMPANION_EXTENSIONS := ["vvd", "vtx", "ani", "phy"]

const FILTERS := [
	{"label": "Everything", "ext": []},
	{"label": "Characters & props", "ext": ["mdl", "p3d"]},
	{"label": "Maps & terrain", "ext": ["bsp", "wrp"]},
	{"label": "Textures & materials", "ext": ["vtf", "vmt", "paa", "tga", "png"]},
	{"label": "Sounds", "ext": ["wav", "mp3", "ogg"]},
]

var _plugin: EditorPlugin

var _vfs: VFSManager
var _importer: UnifiedAssetImporter

var _games: Tree
var _search: LineEdit
var _filter: OptionButton
var _results: Tree
var _picked_name: Label
var _picked_kind: Label
var _pose_row: VBoxContainer
var _pose_picker: OptionButton
var _add_button: Button
var _status: Label

var _selected_uri := ""
var _detected_games: Dictionary = {}
var _file_dialog: EditorFileDialog

## Display name -> { "path": String, "mounts": [ {prefix, path, is_directory} ] }
##
## A game is one thing to the user even though it costs several mounts: Garry's Mod is
## two VPKs plus a loose-file tree. Grouping is what turns three cryptic rows —
## fallbacks_dir, garrysmod_dir, garry's_mod — into one that says "Garry's Mod".
var _sources: Dictionary = {}


func set_editor_plugin(plugin: EditorPlugin) -> void:
	_plugin = plugin


func _ready() -> void:
	custom_minimum_size = Vector2(290, 0)
	add_theme_constant_override("separation", 0)

	_vfs = VFSManager.new()
	_vfs.name = "DockVFS"
	add_child(_vfs)

	_importer = UnifiedAssetImporter.new()
	_importer.name = "DockImporter"
	add_child(_importer)
	_importer.set_vfs(_vfs)

	_build_ui()
	var welcome := _restore_sources()
	_refresh_games()
	# Populate the list before the greeting, or _refresh_results() overwrites it and the
	# user is told a match count instead of being told their games came back.
	_refresh_results()
	if not welcome.is_empty():
		_say(welcome, welcome.ends_with("computer."))


# ---------------------------------------------------------------------------- UI ----

## A dimmed all-caps run-in heading, the same idiom the editor's own inspector uses to
## separate groups. The screen previously ran mount list, search and details together
## with nothing but a rule between them, and everything read as one undifferentiated pile.
func _section(title: String) -> void:
	var margin := MarginContainer.new()
	margin.add_theme_constant_override("margin_top", 10)
	margin.add_theme_constant_override("margin_bottom", 2)
	margin.add_theme_constant_override("margin_left", 4)
	add_child(margin)

	var label := Label.new()
	label.text = title.to_upper()
	label.add_theme_font_size_override("font_size", 10)
	label.add_theme_color_override("font_color", Color(0.62, 0.66, 0.74))
	margin.add_child(label)


func _row() -> HBoxContainer:
	var margin := MarginContainer.new()
	margin.add_theme_constant_override("margin_left", 4)
	margin.add_theme_constant_override("margin_right", 4)
	add_child(margin)
	var box := HBoxContainer.new()
	margin.add_child(box)
	return box


func _build_ui() -> void:
	# ---------------------------------------------------------------- your games ----
	_section("Your games")

	var add_row := _row()
	var add_button := MenuButton.new()
	add_button.text = "Add a game"
	add_button.icon = _icon("Add")
	add_button.flat = false
	add_button.tooltip_text = "Mount a game you have installed, a folder, or a single archive file"
	add_button.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	add_button.about_to_popup.connect(_rebuild_add_menu)
	add_button.get_popup().id_pressed.connect(_on_add_menu_pressed)
	add_row.add_child(add_button)

	var games_margin := MarginContainer.new()
	games_margin.add_theme_constant_override("margin_left", 4)
	games_margin.add_theme_constant_override("margin_right", 4)
	games_margin.add_theme_constant_override("margin_top", 4)
	add_child(games_margin)

	_games = Tree.new()
	_games.custom_minimum_size = Vector2(0, 84)
	_games.hide_root = true
	_games.columns = 2
	_games.set_column_expand(1, false)
	_games.set_column_custom_minimum_width(1, 26)
	_games.button_clicked.connect(_on_remove_game)
	games_margin.add_child(_games)

	# ------------------------------------------------------------ find something ----
	_section("Find something")

	var search_margin := MarginContainer.new()
	search_margin.add_theme_constant_override("margin_left", 4)
	search_margin.add_theme_constant_override("margin_right", 4)
	add_child(search_margin)
	var search_box := VBoxContainer.new()
	search_margin.add_child(search_box)

	_search = LineEdit.new()
	_search.placeholder_text = "Type a name, like police or dust"
	_search.right_icon = _icon("Search")
	_search.clear_button_enabled = true
	_search.text_changed.connect(func(_t: String) -> void: _refresh_results())
	search_box.add_child(_search)

	_filter = OptionButton.new()
	for f in FILTERS:
		_filter.add_item(str((f as Dictionary)["label"]))
	_filter.item_selected.connect(func(_i: int) -> void: _refresh_results())
	search_box.add_child(_filter)

	var results_margin := MarginContainer.new()
	results_margin.add_theme_constant_override("margin_left", 4)
	results_margin.add_theme_constant_override("margin_right", 4)
	results_margin.add_theme_constant_override("margin_top", 4)
	results_margin.size_flags_vertical = Control.SIZE_EXPAND_FILL
	add_child(results_margin)

	_results = Tree.new()
	_results.hide_root = true
	_results.columns = 1
	_results.custom_minimum_size = Vector2(0, 170)
	_results.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_results.item_selected.connect(_on_result_selected)
	_results.item_activated.connect(_on_add_pressed)
	results_margin.add_child(_results)

	# -------------------------------------------------------------- what you got ----
	_section("What you picked")

	var picked_margin := MarginContainer.new()
	picked_margin.add_theme_constant_override("margin_left", 4)
	picked_margin.add_theme_constant_override("margin_right", 4)
	add_child(picked_margin)
	var picked := VBoxContainer.new()
	picked_margin.add_child(picked)

	_picked_name = Label.new()
	_picked_name.text = "Nothing yet"
	_picked_name.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	picked.add_child(_picked_name)

	_picked_kind = Label.new()
	_picked_kind.text = "Pick something from the list above."
	_picked_kind.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_picked_kind.add_theme_font_size_override("font_size", 11)
	_picked_kind.add_theme_color_override("font_color", Color(0.62, 0.66, 0.74))
	picked.add_child(_picked_kind)

	_pose_row = VBoxContainer.new()
	_pose_row.visible = false
	picked.add_child(_pose_row)
	var pose_label := Label.new()
	pose_label.text = "Standing pose"
	pose_label.add_theme_font_size_override("font_size", 11)
	_pose_row.add_child(pose_label)
	_pose_picker = OptionButton.new()
	_pose_picker.tooltip_text = "Which frame of the game's own animations the model stands in"
	_pose_row.add_child(_pose_picker)

	var button_row := _row()
	_add_button = Button.new()
	_add_button.text = "Add to scene"
	_add_button.icon = _icon("Add")
	_add_button.disabled = true
	_add_button.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_add_button.pressed.connect(_on_add_pressed)
	button_row.add_child(_add_button)

	var status_margin := MarginContainer.new()
	status_margin.add_theme_constant_override("margin_left", 4)
	status_margin.add_theme_constant_override("margin_right", 4)
	status_margin.add_theme_constant_override("margin_top", 4)
	status_margin.add_theme_constant_override("margin_bottom", 4)
	add_child(status_margin)

	_status = Label.new()
	_status.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_status.add_theme_font_size_override("font_size", 11)
	status_margin.add_child(_status)

	_say("Start by adding a game you have installed.")


## EditorIcons only exists inside the editor; outside it (the headless dock test) this
## returns null and controls simply render without an icon.
func _icon(name: String) -> Texture2D:
	if has_theme_icon(name, "EditorIcons"):
		return get_theme_icon(name, "EditorIcons")
	return null


func _say(text: String, is_problem: bool = false) -> void:
	_status.text = text
	_status.remove_theme_color_override("font_color")
	if is_problem:
		_status.add_theme_color_override("font_color", Color(1.0, 0.47, 0.42))
	else:
		_status.add_theme_color_override("font_color", Color(0.62, 0.66, 0.74))


## "25990" is hard to read at a glance; "25,990" is not.
func _grouped(n: int) -> String:
	var s := str(n)
	var out := ""
	var count := 0
	for i in range(s.length() - 1, -1, -1):
		out = s[i] + out
		count += 1
		if count % 3 == 0 and i > 0:
			out = "," + out
	return out


# ------------------------------------------------------------------- add a game ----

func _rebuild_add_menu() -> void:
	var button := _find_add_button()
	if button == null:
		return
	var popup := button.get_popup()
	popup.clear()

	_detected_games = SteamLibraryDetector.detect_installed_games()
	var id := 0
	if _detected_games.is_empty():
		popup.add_item("No installed games found automatically", 1000)
		popup.set_item_disabled(popup.get_item_count() - 1, true)
	else:
		for key in _detected_games.keys():
			popup.add_item(str(key), id)
			id += 1
	popup.add_separator()
	popup.add_item("Choose a game folder...", 1001)
	popup.add_item("Open one archive file...", 1002)


func _find_add_button() -> MenuButton:
	for child in get_children():
		if child is MarginContainer:
			for inner in child.get_children():
				if inner is HBoxContainer:
					for c in inner.get_children():
						if c is MenuButton:
							return c
	return null


func _on_add_menu_pressed(id: int) -> void:
	match id:
		1000:
			pass
		1001:
			_open_dialog(EditorFileDialog.FILE_MODE_OPEN_DIR)
		1002:
			_open_dialog(EditorFileDialog.FILE_MODE_OPEN_FILE)
		_:
			var names := _detected_games.keys()
			if id >= 0 and id < names.size():
				var game := str(names[id])
				_add_game_folder(str(_detected_games[game]), game)


func _open_dialog(mode: int) -> void:
	if _file_dialog == null:
		_file_dialog = EditorFileDialog.new()
		_file_dialog.access = EditorFileDialog.ACCESS_FILESYSTEM
		_file_dialog.dir_selected.connect(func(d: String) -> void: _add_game_folder(d, d.get_file()))
		_file_dialog.file_selected.connect(_add_archive)
		add_child(_file_dialog)
	_file_dialog.file_mode = mode
	_file_dialog.clear_filters()
	if mode == EditorFileDialog.FILE_MODE_OPEN_FILE:
		_file_dialog.add_filter("*.vpk, *.wad, *.gma, *.pbo", "Game archives")
	_file_dialog.popup_centered_ratio(0.6)


func _add_game_folder(real_dir: String, label: String) -> void:
	if _sources.has(label):
		_say("%s is already in the list." % label)
		return

	var scan: Dictionary = _vfs.scan_game_directory(real_dir)
	if scan.has("error"):
		_say("Could not read that folder. Check it still exists.", true)
		return

	var mounts := []
	for entry in scan.get("archives", []):
		var path := str(entry)
		var prefix := _unique_prefix(path.get_file().get_basename())
		if _vfs.mount_container(prefix, path):
			mounts.append({"prefix": prefix, "path": path, "is_directory": false})

	# Loose files matter too: Counter-Strike 1.6 keeps its maps as plain .bsp on disk, and
	# an extracted mod folder is the normal case for modders.
	var loose_total := int(scan.get("loose_models", 0)) + int(scan.get("loose_maps", 0)) \
		+ int(scan.get("loose_textures", 0))
	if loose_total > 0:
		var prefix := _unique_prefix(label)
		if _vfs.mount_directory(prefix, real_dir) > 0:
			mounts.append({"prefix": prefix, "path": real_dir, "is_directory": true})

	if mounts.is_empty():
		_say("Nothing importable in that folder.", true)
		return

	_sources[label] = {"path": real_dir, "mounts": mounts}
	_save_sources()
	_refresh_games()
	_refresh_results()
	_say("Added %s. Search for something above." % label)


func _add_archive(path: String) -> void:
	var label := path.get_file()
	if _sources.has(label):
		_say("%s is already in the list." % label)
		return

	var prefix := _unique_prefix(path.get_file().get_basename())
	if not _vfs.mount_container(prefix, path):
		_say("%s is not a game archive Quebratsk can read." % label, true)
		return

	_sources[label] = {
		"path": path,
		"mounts": [{"prefix": prefix, "path": path, "is_directory": false}],
	}
	_save_sources()
	_refresh_games()
	_refresh_results()
	_say("Added %s." % label)


func _unique_prefix(base: String) -> String:
	var clean := base.to_lower().replace(" ", "_")
	if clean.is_empty():
		clean = "content"
	var taken := {}
	for m in _vfs.get_mounts_info():
		taken[str((m as Dictionary).get("prefix", ""))] = true
	if not taken.has(clean):
		return clean
	var n := 2
	while taken.has("%s_%d" % [clean, n]):
		n += 1
	return "%s_%d" % [clean, n]


# ----------------------------------------------------------------- games list ----

func _refresh_games() -> void:
	_games.clear()
	var root := _games.create_item()

	if _sources.is_empty():
		var empty := _games.create_item(root)
		empty.set_text(0, "Nothing added yet")
		empty.set_selectable(0, false)
		empty.set_custom_color(0, Color(0.55, 0.58, 0.64))
		return

	# One row per thing the user added, however many archives it took internally.
	var per_prefix := {}
	for m in _vfs.get_mounts_info():
		var info: Dictionary = m
		per_prefix[str(info.get("prefix", ""))] = int(info.get("file_count", 0))

	for key in _sources.keys():
		var label := str(key)
		var group: Dictionary = _sources[label]
		var files := 0
		var parts := 0
		for entry in group.get("mounts", []):
			var mount: Dictionary = entry
			files += int(per_prefix.get(str(mount.get("prefix", "")), 0))
			parts += 1

		var item := _games.create_item(root)
		item.set_icon(0, _icon("FileList"))
		item.set_text(0, "%s      %s files" % [label, _grouped(files)])
		item.set_tooltip_text(0, "%s\n%d archive%s mounted"
			% [group.get("path", ""), parts, "" if parts == 1 else "s"])
		if _icon("Remove") != null:
			item.add_button(1, _icon("Remove"), 0, false, "Remove %s" % label)
		item.set_metadata(0, label)


func _on_remove_game(item: TreeItem, _col: int, _id: int, _mouse: int) -> void:
	var label := str(item.get_metadata(0))
	if not _sources.has(label):
		return
	for entry in (_sources[label] as Dictionary).get("mounts", []):
		_vfs.unmount(str((entry as Dictionary).get("prefix", "")))
	_sources.erase(label)
	_save_sources()
	_refresh_games()
	_refresh_results()
	_say("Removed %s." % label)


# ------------------------------------------------------------------ persistence ----

func _save_sources() -> void:
	var cfg := ConfigFile.new()
	# Load first: the file also carries the one-time "introduced" flag plugin.gd sets.
	cfg.load(MOUNTS_FILE)
	cfg.set_value("mounts", "sources", _sources)
	cfg.save(MOUNTS_FILE)


## Returns the line to greet the user with, or "" when there was nothing to restore.
func _restore_sources() -> String:
	var cfg := ConfigFile.new()
	if cfg.load(MOUNTS_FILE) != OK:
		return ""

	var saved = cfg.get_value("mounts", "sources", {})
	if typeof(saved) != TYPE_DICTIONARY:
		return "" # written by an older version; start clean rather than half-restore

	var restored := 0
	var gone := 0
	for key in (saved as Dictionary).keys():
		var label := str(key)
		var group: Dictionary = (saved as Dictionary)[key]
		var live := []
		for entry in group.get("mounts", []):
			var mount: Dictionary = entry
			var path := str(mount.get("path", ""))
			var prefix := str(mount.get("prefix", ""))
			if path.is_empty() or prefix.is_empty():
				continue
			# A game can be uninstalled between sessions.
			if bool(mount.get("is_directory", false)):
				if DirAccess.dir_exists_absolute(path) and _vfs.mount_directory(prefix, path) > 0:
					live.append(mount)
			elif FileAccess.file_exists(path) and _vfs.mount_container(prefix, path):
				live.append(mount)

		if live.is_empty():
			gone += 1
		else:
			_sources[label] = {"path": group.get("path", ""), "mounts": live}
			restored += 1

	if restored > 0 and gone == 0:
		return "Picking up where you left off."
	if restored > 0:
		return "%d game%s no longer on this computer, so %s removed." \
			% [gone, "" if gone == 1 else "s", "it was" if gone == 1 else "they were"]
	if gone > 0:
		return "The games you added before are no longer on this computer."
	return ""


# ---------------------------------------------------------------------- results ----

func _kind_of(uri: String) -> Dictionary:
	var ext := uri.get_extension().to_lower()
	if KINDS.has(ext):
		return KINDS[ext]
	return {"name": ext.to_upper() + " file", "icon": "File", "placeable": false}


func _refresh_results() -> void:
	_results.clear()
	_clear_selection()

	var root := _results.create_item()
	var needle := _search.text.strip_edges().to_lower()
	var wanted: Array = (FILTERS[_filter.selected] as Dictionary)["ext"]

	var all: PackedStringArray = _vfs.list_files()
	if all.is_empty():
		var hint := _results.create_item(root)
		hint.set_text(0, "Add a game to see what is inside it")
		hint.set_selectable(0, false)
		hint.set_custom_color(0, Color(0.55, 0.58, 0.64))
		return

	var shown := 0
	var matched := 0
	for uri in all:
		var ext := uri.get_extension().to_lower()
		if COMPANION_EXTENSIONS.has(ext):
			continue
		if not wanted.is_empty() and not wanted.has(ext):
			continue
		if not needle.is_empty() and not uri.to_lower().contains(needle):
			continue
		matched += 1
		if shown >= MAX_RESULTS:
			continue

		var item := _results.create_item(root)
		var kind := _kind_of(uri)
		item.set_icon(0, _icon(str(kind["icon"])))
		# The name is what a person scans for; the folder is context, so it is dimmed and
		# secondary rather than the whole row being one long unreadable path.
		item.set_text(0, uri.get_file())
		item.set_tooltip_text(0, uri)
		item.set_metadata(0, uri)
		shown += 1

	if matched == 0:
		var none := _results.create_item(root)
		none.set_text(0, "Nothing found")
		none.set_selectable(0, false)
		none.set_custom_color(0, Color(0.55, 0.58, 0.64))
		_say("No matches. Try a shorter word, or a different category.")
	elif matched > shown:
		_say("Showing the first %s of %s. Keep typing to narrow it down."
			% [_grouped(shown), _grouped(matched)])
	else:
		_say("%s item%s." % [_grouped(matched), "" if matched == 1 else "s"])


func _clear_selection() -> void:
	_selected_uri = ""
	_add_button.disabled = true
	_pose_row.visible = false
	_picked_name.text = "Nothing yet"
	_picked_kind.text = "Pick something from the list above."


func _on_result_selected() -> void:
	var item := _results.get_selected()
	if item == null:
		return
	var uri := str(item.get_metadata(0))
	if uri.is_empty():
		return

	_selected_uri = uri
	var kind := _kind_of(uri)
	_picked_name.text = uri.get_file()

	var where := uri.trim_prefix("vfs://").get_base_dir()
	var slash := where.find("/")
	if slash >= 0:
		where = where.substr(slash + 1)
	_picked_kind.text = "%s · %s · %s" % [kind["name"],
		String.humanize_size(_vfs.get_file_size(uri)), where]

	_pose_row.visible = false
	if uri.get_extension().to_lower() == "mdl":
		_load_poses(uri)

	if bool(kind["placeable"]):
		_add_button.disabled = false
		_say("Ready. Press Add to scene.")
	else:
		_add_button.disabled = true
		_say("%ss can be found here, but they are used by models and maps rather than \
placed on their own." % kind["name"])


## Poses live in the .mdl and its .ani, so this skips the vertex data — but it is not
## free (~70 ms for a Garry's Mod player model). Called on selection only, never while
## typing in the search box.
func _load_poses(uri: String) -> void:
	var poses: PackedStringArray = _importer.list_poses(uri)
	_pose_picker.clear()
	if poses.is_empty():
		return
	_pose_picker.add_item("Whatever the game uses by default")
	_pose_picker.set_item_metadata(0, "")
	for p in poses:
		_pose_picker.add_item(p)
		_pose_picker.set_item_metadata(_pose_picker.item_count - 1, p)
	_pose_picker.select(0)
	_pose_row.visible = true


# ----------------------------------------------------------------------- import ----

func _on_add_pressed() -> void:
	if _selected_uri.is_empty() or _add_button.disabled or _plugin == null:
		return

	var scene_root := _plugin.get_editor_interface().get_edited_scene_root()
	if scene_root == null:
		_say("Open a scene first — then press Add to scene again.", true)
		return

	var ext := _selected_uri.get_extension().to_lower()
	var node: Node3D = null

	if ext == "bsp" or ext == "wrp":
		var mesh: ArrayMesh = _importer.load_mesh(_selected_uri)
		if mesh != null:
			var mi := MeshInstance3D.new()
			mi.mesh = mesh
			mi.name = _selected_uri.get_file().get_basename()
			node = mi
	else:
		var pose := ""
		if _pose_row.visible and _pose_picker.selected >= 0:
			pose = str(_pose_picker.get_item_metadata(_pose_picker.selected))
		node = _importer.load_model(_selected_uri, pose)

	if node == null:
		_say(_explain_failure(), true)
		return

	var undo := _plugin.get_undo_redo()
	undo.create_action("Add %s" % _selected_uri.get_file())
	undo.add_do_method(scene_root, "add_child", node, true)
	undo.add_do_reference(node)
	undo.add_undo_method(scene_root, "remove_child", node)
	undo.commit_action()
	# owner must be set on every descendant or the branch vanishes when the scene is
	# saved and reopened.
	_claim_ownership(node, scene_root)

	_plugin.get_editor_interface().get_selection().clear()
	_plugin.get_editor_interface().get_selection().add_node(node)
	_say("Added %s. It is selected in the scene now." % node.name)


func _claim_ownership(node: Node, scene_root: Node) -> void:
	node.owner = scene_root
	for child in node.get_children():
		_claim_ownership(child, scene_root)


## Say what went wrong in terms of something the user can do about it.
func _explain_failure() -> String:
	match _importer.get_last_error_code():
		UnifiedAssetImporter.ERR_ASSET_UNREADABLE:
			return "That file could not be read. Was the game moved or uninstalled?"
		UnifiedAssetImporter.ERR_PARSE_FAILED:
			if _selected_uri.get_extension().to_lower() == "mdl":
				return "This model keeps its shape in companion files that are not here. \
Add the rest of the game's archives and try again."
			return "This file was recognised but nothing could be read out of it."
		UnifiedAssetImporter.ERR_VFS_NOT_SET:
			return "Something went wrong inside the plugin. Disable and re-enable it."
		_:
			return "That did not work. The Output panel at the bottom has the details."
