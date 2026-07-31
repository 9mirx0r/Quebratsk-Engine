@tool
extends VBoxContainer

## The Quebratsk dock.
##
## Designed for someone who has never used Godot: pick an installed game, search for a
## thing, press Add. No URIs to type, no extraction step, no conversion tools.
##
## Everything here talks to the GDExtension through the surface documented in
## docs/API.md. If a call below disagrees with that file, the file is the bug.

const MOUNTS_FILE := "user://quebratsk_mounts.cfg"

## list_files() with no prefix returns every indexed entry — a Half-Life 2 plus Garry's
## Mod setup is over 100,000. Populating a Tree with that locks the editor, so results are
## capped and the user is told to narrow the search.
const MAX_RESULTS := 400

## Array literals are constant expressions in GDScript; a PackedStringArray() call is not,
## so these stay plain Arrays.
const FILTERS := {
	"Everything": [],
	"Models": ["mdl", "p3d"],
	"Maps": ["bsp", "wrp"],
	"Textures & materials": ["vtf", "vmt", "paa", "tga", "png"],
	"Sounds": ["wav", "mp3", "ogg"],
}

var _plugin: EditorPlugin

var _vfs: VFSManager
var _importer: UnifiedAssetImporter

var _add_button: MenuButton
var _mount_list: Tree
var _search: LineEdit
var _filter: OptionButton
var _results: Tree
var _detail_path: Label
var _pose_row: HBoxContainer
var _pose_picker: OptionButton
var _add_to_scene: Button
var _status: Label

var _selected_uri := ""
var _detected_games: Dictionary = {}
var _file_dialog: EditorFileDialog


func set_editor_plugin(plugin: EditorPlugin) -> void:
	_plugin = plugin


func _ready() -> void:
	custom_minimum_size = Vector2(280, 0)
	add_theme_constant_override("separation", 6)

	_vfs = VFSManager.new()
	_vfs.name = "DockVFS"
	add_child(_vfs)

	_importer = UnifiedAssetImporter.new()
	_importer.name = "DockImporter"
	add_child(_importer)
	_importer.set_vfs(_vfs)

	_build_ui()
	_restore_mounts()
	_refresh_mounts()
	# Without this the file list is empty on open even when content was restored, so the
	# dock looks like it has nothing in it until the user happens to type something.
	_refresh_results()


# ---------------------------------------------------------------------------- UI ----

func _build_ui() -> void:
	var header := HBoxContainer.new()
	add_child(header)

	_add_button = MenuButton.new()
	_add_button.text = "  Add game content  "
	_add_button.tooltip_text = "Mount an installed game, a folder, or a single archive"
	_add_button.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_add_button.about_to_popup.connect(_rebuild_add_menu)
	_add_button.get_popup().id_pressed.connect(_on_add_menu_pressed)
	header.add_child(_add_button)

	_mount_list = Tree.new()
	_mount_list.custom_minimum_size = Vector2(0, 92)
	_mount_list.hide_root = true
	_mount_list.columns = 2
	_mount_list.set_column_expand(1, false)
	_mount_list.set_column_custom_minimum_width(1, 28)
	_mount_list.button_clicked.connect(_on_mount_button)
	add_child(_mount_list)

	add_child(HSeparator.new())

	_search = LineEdit.new()
	_search.placeholder_text = "Search, e.g. police or de_dust"
	_search.clear_button_enabled = true
	_search.text_changed.connect(_on_search_changed)
	add_child(_search)

	_filter = OptionButton.new()
	for name in FILTERS.keys():
		_filter.add_item(str(name))
	_filter.item_selected.connect(func(_i: int) -> void: _refresh_results())
	add_child(_filter)

	_results = Tree.new()
	_results.hide_root = true
	_results.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_results.custom_minimum_size = Vector2(0, 160)
	_results.item_selected.connect(_on_result_selected)
	_results.item_activated.connect(_on_add_pressed)
	add_child(_results)

	add_child(HSeparator.new())

	_detail_path = Label.new()
	_detail_path.text = "Nothing selected"
	_detail_path.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_detail_path.add_theme_font_size_override("font_size", 11)
	add_child(_detail_path)

	_pose_row = HBoxContainer.new()
	_pose_row.visible = false
	add_child(_pose_row)
	var pose_label := Label.new()
	pose_label.text = "Pose"
	_pose_row.add_child(pose_label)
	_pose_picker = OptionButton.new()
	_pose_picker.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_pose_picker.tooltip_text = "Animation frame the model stands in when imported"
	_pose_row.add_child(_pose_picker)

	_add_to_scene = Button.new()
	_add_to_scene.text = "Add to scene"
	_add_to_scene.disabled = true
	_add_to_scene.pressed.connect(_on_add_pressed)
	add_child(_add_to_scene)

	_status = Label.new()
	_status.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_status.add_theme_font_size_override("font_size", 11)
	add_child(_status)

	_set_status("Press \"Add game content\" to begin.")


func _set_status(text: String, is_error: bool = false) -> void:
	_status.text = text
	_status.remove_theme_color_override("font_color")
	if is_error:
		_status.add_theme_color_override("font_color", Color(1.0, 0.45, 0.4))


# ------------------------------------------------------------------- add content ----

func _rebuild_add_menu() -> void:
	var popup := _add_button.get_popup()
	popup.clear()

	_detected_games = SteamLibraryDetector.detect_installed_games()
	var id := 0
	if _detected_games.is_empty():
		popup.add_item("No Steam games detected", 1000)
		popup.set_item_disabled(popup.get_item_count() - 1, true)
	else:
		for key in _detected_games.keys():
			popup.add_item(str(key), id)
			id += 1
	popup.add_separator()
	popup.add_item("Browse for a game folder...", 1001)
	popup.add_item("Open a single archive...", 1002)


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
				_mount_game_folder(str(_detected_games[game]), game)


func _open_dialog(mode: int) -> void:
	if _file_dialog == null:
		_file_dialog = EditorFileDialog.new()
		_file_dialog.access = EditorFileDialog.ACCESS_FILESYSTEM
		_file_dialog.dir_selected.connect(func(d: String) -> void: _mount_game_folder(d, d.get_file()))
		_file_dialog.file_selected.connect(_mount_single_archive)
		add_child(_file_dialog)
	_file_dialog.file_mode = mode
	_file_dialog.clear_filters()
	if mode == EditorFileDialog.FILE_MODE_OPEN_FILE:
		_file_dialog.add_filter("*.vpk, *.wad, *.gma, *.pbo", "Game archives")
	_file_dialog.popup_centered_ratio(0.6)


## Scan a game folder, mount every archive it holds, and index its loose files too.
func _mount_game_folder(real_dir: String, label: String) -> void:
	var scan: Dictionary = _vfs.scan_game_directory(real_dir)
	if scan.has("error"):
		_set_status("Could not read %s: %s" % [real_dir, scan["error"]], true)
		return

	var archives: Array = scan.get("archives", [])
	var mounted := 0
	for entry in archives:
		var path := str(entry)
		var prefix := _unique_prefix(path.get_file().get_basename())
		if _vfs.mount_container(prefix, path):
			mounted += 1

	# Loose files matter too: Counter-Strike 1.6 keeps its maps as plain .bsp on disk, and
	# extracted mod folders are the normal case for modders.
	var loose := 0
	var loose_total := int(scan.get("loose_models", 0)) + int(scan.get("loose_maps", 0)) \
		+ int(scan.get("loose_textures", 0))
	if loose_total > 0:
		loose = _vfs.mount_directory(_unique_prefix(label), real_dir)

	_save_mounts()
	_refresh_mounts()
	_refresh_results()

	if mounted == 0 and loose == 0:
		_set_status("Nothing importable found in %s." % real_dir, true)
	else:
		_set_status("%s: %d archive%s and %d loose file%s added."
			% [label, mounted, "" if mounted == 1 else "s", loose, "" if loose == 1 else "s"])


func _mount_single_archive(path: String) -> void:
	var prefix := _unique_prefix(path.get_file().get_basename())
	if _vfs.mount_container(prefix, path):
		_save_mounts()
		_refresh_mounts()
		_refresh_results()
		_set_status("Added %s." % path.get_file())
	else:
		_set_status("%s is not a WAD, VPK, GMA or PBO archive." % path.get_file(), true)


func _unique_prefix(base: String) -> String:
	var clean := base.to_lower().replace(" ", "_")
	if clean.is_empty():
		clean = "content"
	var taken := {}
	for m in _vfs.get_mounts_info():
		var info: Dictionary = m
		taken[str(info.get("prefix", ""))] = true
	if not taken.has(clean):
		return clean
	var n := 2
	while taken.has("%s_%d" % [clean, n]):
		n += 1
	return "%s_%d" % [clean, n]


# ------------------------------------------------------------------------ mounts ----

func _refresh_mounts() -> void:
	_mount_list.clear()
	var root := _mount_list.create_item()
	var mounts: Array = _vfs.get_mounts_info()

	if mounts.is_empty():
		var empty := _mount_list.create_item(root)
		empty.set_text(0, "No content added yet")
		empty.set_selectable(0, false)
		empty.set_custom_color(0, Color(0.6, 0.6, 0.6))
		return

	for m in mounts:
		var info: Dictionary = m
		var item := _mount_list.create_item(root)
		item.set_text(0, "%s  —  %s files" % [info.get("prefix", "?"),
			String.num_uint64(int(info.get("file_count", 0)))])
		item.set_tooltip_text(0, "%s\n%s" % [info.get("real_path", ""), info.get("engine", "")])
		# EditorIcons only exists inside the editor; outside it (the headless dock test)
		# has_theme_icon() is false and the row simply has no remove button.
		if has_theme_icon("Remove", "EditorIcons"):
			item.add_button(1, get_theme_icon("Remove", "EditorIcons"), 0, false, "Remove")
		item.set_metadata(0, info.get("prefix", ""))


func _on_mount_button(item: TreeItem, _col: int, _id: int, _mouse: int) -> void:
	var prefix := str(item.get_metadata(0))
	if prefix.is_empty():
		return
	_vfs.unmount(prefix)
	_save_mounts()
	_refresh_mounts()
	_refresh_results()
	_set_status("Removed %s." % prefix)


# ---- persistence: paths are machine-specific, so they live in user:// not the project --

func _save_mounts() -> void:
	var cfg := ConfigFile.new()
	# Load before saving: the file also carries the one-time "introduced" flag that
	# plugin.gd sets, and a blank ConfigFile would drop it on every mount change.
	cfg.load(MOUNTS_FILE)
	var rows := []
	for m in _vfs.get_mounts_info():
		var info: Dictionary = m
		rows.append({
			"path": str(info.get("real_path", "")),
			"prefix": str(info.get("prefix", "")),
			# An archive and a directory need different mount calls, and the path alone
			# cannot tell them apart once the game is uninstalled.
			"is_directory": bool(info.get("is_directory", false)),
		})
	cfg.set_value("mounts", "sources", rows)
	cfg.save(MOUNTS_FILE)


func _restore_mounts() -> void:
	var cfg := ConfigFile.new()
	if cfg.load(MOUNTS_FILE) != OK:
		return

	var rows: Array = cfg.get_value("mounts", "sources", [])
	var restored := 0
	var missing := 0
	for r in rows:
		var row: Dictionary = r
		var path := str(row.get("path", ""))
		var prefix := str(row.get("prefix", ""))
		if path.is_empty() or prefix.is_empty():
			continue

		if bool(row.get("is_directory", false)):
			# A game can be uninstalled between sessions; skip rather than erroring.
			if not DirAccess.dir_exists_absolute(path):
				missing += 1
				continue
			if _vfs.mount_directory(prefix, path) > 0:
				restored += 1
		else:
			if not FileAccess.file_exists(path):
				missing += 1
				continue
			if _vfs.mount_container(prefix, path):
				restored += 1

	if restored > 0 or missing > 0:
		var msg := "Restored %d source%s from your last session." \
			% [restored, "" if restored == 1 else "s"]
		if missing > 0:
			msg += " %d no longer exist%s on disk." % [missing, "s" if missing == 1 else ""]
		_set_status(msg)


# ----------------------------------------------------------------------- results ----

func _on_search_changed(_text: String) -> void:
	_refresh_results()


func _refresh_results() -> void:
	_results.clear()
	_selected_uri = ""
	_add_to_scene.disabled = true
	_pose_row.visible = false
	_detail_path.text = "Nothing selected"

	var root := _results.create_item()
	var needle := _search.text.strip_edges().to_lower()
	var wanted: Array = FILTERS.values()[_filter.selected]

	var all: PackedStringArray = _vfs.list_files()
	if all.is_empty():
		var hint := _results.create_item(root)
		hint.set_text(0, "Add game content to see files here")
		hint.set_selectable(0, false)
		hint.set_custom_color(0, Color(0.6, 0.6, 0.6))
		return

	var shown := 0
	var matched := 0
	for uri in all:
		if not wanted.is_empty() and not wanted.has(uri.get_extension().to_lower()):
			continue
		if not needle.is_empty() and not uri.to_lower().contains(needle):
			continue
		matched += 1
		if shown >= MAX_RESULTS:
			continue
		var item := _results.create_item(root)
		item.set_text(0, uri.trim_prefix("vfs://"))
		item.set_tooltip_text(0, uri)
		item.set_metadata(0, uri)
		shown += 1

	if matched == 0:
		var none := _results.create_item(root)
		none.set_text(0, "No matches")
		none.set_selectable(0, false)
		none.set_custom_color(0, Color(0.6, 0.6, 0.6))
		_set_status("Nothing matched. Try a shorter search term.")
	elif matched > shown:
		_set_status("Showing %d of %d matches — narrow the search to see the rest."
			% [shown, matched])
	else:
		_set_status("%d match%s." % [matched, "" if matched == 1 else "es"])


func _on_result_selected() -> void:
	var item := _results.get_selected()
	if item == null:
		return
	var uri := str(item.get_metadata(0))
	if uri.is_empty():
		return

	_selected_uri = uri
	var size := _vfs.get_file_size(uri)
	_detail_path.text = "%s\n%s" % [uri.get_file(), String.humanize_size(size)]
	_add_to_scene.disabled = false

	var ext := uri.get_extension().to_lower()
	_pose_row.visible = false
	if ext == "mdl":
		_populate_poses(uri)

	if ext in ["mdl", "p3d", "bsp", "wrp"]:
		_add_to_scene.text = "Add to scene"
	else:
		_add_to_scene.text = "Add to scene"
		_add_to_scene.disabled = true
		_set_status("%s files can be browsed but not placed in a scene yet." % ext.to_upper())


## Poses live in the .mdl and its .ani, so this skips the vertex data — but it is not
## free (~70 ms for a Garry's Mod player model, dominated by the 7 MB m_anm.ani it
## borrows from). Called only on selection, never while typing in the search box.
func _populate_poses(uri: String) -> void:
	var poses: PackedStringArray = _importer.list_poses(uri)
	_pose_picker.clear()
	if poses.is_empty():
		return
	_pose_picker.add_item("Automatic")
	_pose_picker.set_item_metadata(0, "")
	for p in poses:
		_pose_picker.add_item(p)
		_pose_picker.set_item_metadata(_pose_picker.item_count - 1, p)
	_pose_picker.select(0)
	_pose_row.visible = true


# -------------------------------------------------------------------- import ----

func _on_add_pressed() -> void:
	if _selected_uri.is_empty() or _plugin == null:
		return

	var scene_root := _plugin.get_editor_interface().get_edited_scene_root()
	if scene_root == null:
		_set_status("Open or create a scene first, then press Add again.", true)
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
		_set_status(_explain_failure(), true)
		return

	# Owner must be set on every descendant or the branch vanishes when the scene is
	# saved and reopened.
	var undo := _plugin.get_undo_redo()
	undo.create_action("Add %s" % _selected_uri.get_file())
	undo.add_do_method(scene_root, "add_child", node, true)
	undo.add_do_reference(node)
	undo.add_undo_method(scene_root, "remove_child", node)
	undo.commit_action()
	_claim_ownership(node, scene_root)

	_plugin.get_editor_interface().get_selection().clear()
	_plugin.get_editor_interface().get_selection().add_node(node)
	_set_status("Added %s to %s." % [node.name, scene_root.name])


func _claim_ownership(node: Node, scene_root: Node) -> void:
	node.owner = scene_root
	for child in node.get_children():
		_claim_ownership(child, scene_root)


## Turn the importer's error code into something a person can act on.
func _explain_failure() -> String:
	match _importer.get_last_error_code():
		UnifiedAssetImporter.ERR_ASSET_UNREADABLE:
			return "Could not read that file. Was the game folder moved or uninstalled?"
		UnifiedAssetImporter.ERR_PARSE_FAILED:
			if _selected_uri.get_extension().to_lower() == "mdl":
				return "Nothing could be decoded. A Source model keeps its geometry in a \
separate .vvd and .vtx file — if those are in another archive, add that one too."
			return "The file was recognised but nothing could be decoded from it."
		UnifiedAssetImporter.ERR_VFS_NOT_SET:
			return "Internal error: the importer lost its file system. Reload the plugin."
		_:
			return "Import failed for an unknown reason. Check the Output panel."
