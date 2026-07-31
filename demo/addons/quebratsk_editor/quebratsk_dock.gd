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
const ICON_DIR := "res://addons/quebratsk_editor/icons"
const TRANSLATIONS := "res://addons/quebratsk_editor/i18n/dock.csv"

## list_files() with no prefix returns every indexed entry — a Half-Life 2 plus Garry's
## Mod setup is over 100,000. Populating a Tree with that locks the editor.
const MAX_RESULTS := 400

## Extension -> what a person would call it, and the editor icon that reads as that thing.
const KINDS := {
	"mdl": {"name": "Model", "icon": "type_model", "placeable": true},
	# Arma and DayZ models are listed so the archive can be explored, but their geometry
	# is not decoded yet — saying so here beats letting the user press Add and get an
	# error they cannot act on.
	"p3d": {"name": "Model", "icon": "type_model", "placeable": false,
		"why": "Arma and DayZ models cannot be imported yet — only their maps and archives."},
	"bsp": {"name": "Map", "icon": "type_map", "placeable": true},
	"wrp": {"name": "Terrain", "icon": "type_terrain", "placeable": true},
	"vtf": {"name": "Texture", "icon": "type_texture", "placeable": false},
	"tga": {"name": "Texture", "icon": "type_texture", "placeable": false},
	"png": {"name": "Texture", "icon": "type_texture", "placeable": false},
	"paa": {"name": "Texture", "icon": "type_texture", "placeable": false},
	"vmt": {"name": "Material", "icon": "type_material", "placeable": false},
	"wav": {"name": "Sound", "icon": "type_sound", "placeable": false},
	"mp3": {"name": "Sound", "icon": "type_sound", "placeable": false},
	"ogg": {"name": "Sound", "icon": "type_sound", "placeable": false},
}

## Never listed. These are pieces of another file rather than things in their own right:
## a Source model is split across police.mdl + police.vvd + police.dx90.vtx + police.ani,
## and the importer pulls the companions in by itself. Showing them meant a search for
## "police" returned four rows where the user wanted one, three of which do nothing when
## picked.
const COMPANION_EXTENSIONS := ["vvd", "vtx", "ani", "phy"]

## Ready-made categories, so a user can browse without knowing what to search for.
##
## `ext` narrows by file type; `folders` narrows by where the file sits. Both GoldSrc and
## Source lay content out by convention — models/weapons/, models/player/,
## models/props_vehicles/ — so the folder is a far better guide to what a thing IS than
## its extension, which only says "model". An empty list means "do not narrow on this".
const CATEGORIES := [
	{"label": "Everything", "ext": [], "folders": []},
	{"label": "Characters & people", "ext": ["mdl", "p3d"],
		"folders": ["models/player", "/humans/", "/npc", "/zombie", "/combine_", "/police"]},
	{"label": "Weapons", "ext": ["mdl", "p3d"],
		"folders": ["/weapons/", "/w_", "/v_", "/shells/"]},
	{"label": "Vehicles", "ext": ["mdl", "p3d"],
		"folders": ["vehicle", "/cars/", "/car_", "/airboat", "/buggy", "/jeep"]},
	{"label": "Props & scenery", "ext": ["mdl", "p3d"],
		"folders": ["/props", "/furniture", "/gibs/"]},
	{"label": "All models", "ext": ["mdl", "p3d"], "folders": []},
	{"label": "Maps & terrain", "ext": ["bsp", "wrp"], "folders": []},
	{"label": "Textures & materials", "ext": ["vtf", "vmt", "paa", "tga", "png"], "folders": []},
	{"label": "Sounds", "ext": ["wav", "mp3", "ogg"], "folders": []},
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
var _search_debounce: Timer
var _icon_cache: Dictionary = {}

var _preview_frame: SubViewportContainer
var _preview: SubViewport
var _preview_pivot: Node3D
var _preview_node: Node3D
var _preview_debounce: Timer
## Orbit state, in degrees. Yaw starts three-quarters on so a character is not seen
## dead-on, which reads flat.
var _orbit := Vector2(35, -18)
var _orbit_distance := 3.0

## Display name -> { "path": String, "mounts": [ {prefix, path, is_directory} ] }
##
## A game is one thing to the user even though it costs several mounts: Garry's Mod is
## two VPKs plus a loose-file tree. Grouping is what turns three cryptic rows —
## fallbacks_dir, garrysmod_dir, garry's_mod — into one that says "Garry's Mod".
var _sources: Dictionary = {}


func set_editor_plugin(plugin: EditorPlugin) -> void:
	_plugin = plugin


func _ready() -> void:
	custom_minimum_size = Vector2(310, 0)
	add_theme_constant_override("separation", 0)

	_vfs = VFSManager.new()
	_vfs.name = "DockVFS"
	add_child(_vfs)

	_importer = UnifiedAssetImporter.new()
	_importer.name = "DockImporter"
	add_child(_importer)
	_importer.set_vfs(_vfs)

	_install_translations()
	_build_ui()
	var welcome := _restore_sources()
	_refresh_games()
	# Populate the list before the greeting, or _refresh_results() overwrites it and the
	# user is told a match count instead of being told their games came back.
	_refresh_results()
	if not welcome.is_empty():
		_say(welcome, welcome.ends_with("computer."))


# ------------------------------------------------------------------------ i18n ----

## Register the dock's translations, built from the CSV at startup rather than imported.
##
## Godot's import route produces .translation files that the *project* must list under
## Localization. That works in this repository and breaks for the people who actually use
## this: the addon ships as a zip dropped into someone else's project, whose project.godot
## we do not control. Parsing 60 rows costs nothing and works the moment the folder exists.
##
## Keys are the English strings themselves, so a row that is missing or empty falls back to
## English rather than showing a symbolic key.
func _install_translations() -> void:
	var file := FileAccess.open(TRANSLATIONS, FileAccess.READ)
	if file == null:
		return

	var header := file.get_csv_line()
	if header.size() < 3 or header[0] != "keys":
		return

	# Column 0 is the key; every column after it is a locale.
	var per_locale: Array[Translation] = []
	for i in range(1, header.size()):
		var t := Translation.new()
		t.locale = header[i].strip_edges()
		per_locale.append(t)

	while not file.eof_reached():
		var row := file.get_csv_line()
		if row.size() < header.size() or row[0].is_empty():
			continue
		for i in range(1, header.size()):
			var value := row[i].strip_edges()
			if not value.is_empty():
				per_locale[i - 1].add_message(row[0], value)

	for t in per_locale:
		TranslationServer.add_translation(t)


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
	_section(tr("Your games"))

	var add_row := _row()
	var add_button := MenuButton.new()
	add_button.text = tr("Add a game")
	add_button.icon = _icon("action_add")
	add_button.flat = false
	add_button.tooltip_text = tr("Mount a game you have installed, a folder, or a single archive file")
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
	_section(tr("Find something"))

	var search_margin := MarginContainer.new()
	search_margin.add_theme_constant_override("margin_left", 4)
	search_margin.add_theme_constant_override("margin_right", 4)
	add_child(search_margin)
	var search_box := VBoxContainer.new()
	search_margin.add_child(search_box)

	_search = LineEdit.new()
	_search.placeholder_text = tr("Type a name, like police or dust")
	_search.right_icon = _icon("Search")
	_search.clear_button_enabled = true
	_search.text_changed.connect(_on_search_typed)
	search_box.add_child(_search)

	# Filtering two mounted games means walking ~42,000 entries and rebuilding up to 400
	# rows, about 120 ms. Doing that on every keystroke makes typing feel like it is
	# fighting back, so the rebuild waits until the user pauses.
	_search_debounce = Timer.new()
	_search_debounce.wait_time = 0.18
	_search_debounce.one_shot = true
	_search_debounce.timeout.connect(_refresh_results)
	add_child(_search_debounce)

	_preview_debounce = Timer.new()
	_preview_debounce.wait_time = 0.25
	_preview_debounce.one_shot = true
	_preview_debounce.timeout.connect(_refresh_preview)
	add_child(_preview_debounce)

	_filter = OptionButton.new()
	for f in CATEGORIES:
		_filter.add_item(tr(str((f as Dictionary)["label"])))
	_filter.tooltip_text = tr("Browse by what a thing is, without having to search for it")
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
	# Second column names the game each hit came from. With more than one game mounted,
	# "police.mdl" alone does not tell you whether you are about to import the Half-Life 2
	# one or the Garry's Mod one.
	_results.columns = 2
	_results.set_column_expand(1, false)
	# Wide enough for "/ Garry's Mod" without ellipsis; longer names ellipsise instead,
	# and the tooltip carries the full title either way.
	_results.set_column_custom_minimum_width(1, 112)
	_results.custom_minimum_size = Vector2(0, 170)
	_results.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_results.item_selected.connect(_on_result_selected)
	_results.item_activated.connect(_on_add_pressed)
	results_margin.add_child(_results)

	# -------------------------------------------------------------- what you got ----
	_section(tr("What you picked"))

	var picked_margin := MarginContainer.new()
	picked_margin.add_theme_constant_override("margin_left", 4)
	picked_margin.add_theme_constant_override("margin_right", 4)
	add_child(picked_margin)
	var picked := VBoxContainer.new()
	picked_margin.add_child(picked)

	# A look at the thing before it lands in the scene. Without this the only way to know
	# whether you picked the right police.mdl is to import it, look, and undo.
	_preview_frame = SubViewportContainer.new()
	_preview_frame.stretch = true
	_preview_frame.custom_minimum_size = Vector2(0, 170)
	_preview_frame.visible = false
	_preview_frame.mouse_filter = Control.MOUSE_FILTER_STOP
	_preview_frame.tooltip_text = tr("Drag to turn it around")
	_preview_frame.gui_input.connect(_on_preview_input)
	picked.add_child(_preview_frame)

	_preview = MapPreviewViewport.new()
	_preview.transparent_bg = true
	_preview.own_world_3d = true
	_preview_frame.add_child(_preview)

	var key := DirectionalLight3D.new()
	key.rotation_degrees = Vector3(-38, -35, 0)
	key.light_energy = 1.25
	_preview.add_child(key)

	var fill := DirectionalLight3D.new()
	fill.rotation_degrees = Vector3(-10, 150, 0)
	fill.light_energy = 0.45
	_preview.add_child(fill)

	_preview_pivot = Node3D.new()
	_preview.add_child(_preview_pivot)

	_picked_name = Label.new()
	_picked_name.text = tr("Nothing yet")
	_picked_name.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	picked.add_child(_picked_name)

	_picked_kind = Label.new()
	_picked_kind.text = tr("Pick something from the list above.")
	_picked_kind.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_picked_kind.add_theme_font_size_override("font_size", 11)
	_picked_kind.add_theme_color_override("font_color", Color(0.62, 0.66, 0.74))
	picked.add_child(_picked_kind)

	_pose_row = VBoxContainer.new()
	_pose_row.visible = false
	picked.add_child(_pose_row)
	var pose_label := Label.new()
	pose_label.text = tr("Standing pose")
	pose_label.add_theme_font_size_override("font_size", 11)
	_pose_row.add_child(pose_label)
	_pose_picker = OptionButton.new()
	_pose_picker.tooltip_text = tr("Which frame of the game's own animations the model stands in")
	_pose_row.add_child(_pose_picker)

	var button_row := _row()
	_add_button = Button.new()
	_add_button.text = tr("Add to scene")
	_add_button.icon = _icon("action_add")
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

	_say(tr("Start by adding a game you have installed."))


# -------------------------------------------------------------------- preview ----

## Load the picked asset into the preview viewport.
##
## Debounced, and deliberately not run straight from the selection handler: a full import
## is ~170 ms against ~70 ms for the pose list alone, so arrow-keying down a list of 400
## results would rebuild a model per keypress. The wait means only what you settle on is
## ever loaded.
func _refresh_preview() -> void:
	_clear_preview()

	if _selected_uri.is_empty():
		return
	var kind := _kind_of(_selected_uri)
	if not bool(kind["placeable"]):
		return

	var node: Node3D = null
	var ext := _selected_uri.get_extension().to_lower()
	if ext == "bsp" or ext == "wrp":
		var mesh: ArrayMesh = _importer.load_mesh(_selected_uri)
		if mesh != null:
			var mi := MeshInstance3D.new()
			mi.mesh = mesh
			# Named here rather than on import: the preview node is what ends up in the
			# scene, so it has to arrive already carrying the name the user will see.
			mi.name = _selected_uri.get_file().get_basename()
			node = mi
	else:
		node = _importer.load_model(_selected_uri, _chosen_pose())

	if node == null:
		return

	_preview_node = node
	_preview_pivot.add_child(node)
	_frame_preview(node)
	_preview_frame.visible = true


func _clear_preview() -> void:
	_preview_frame.visible = false
	if is_instance_valid(_preview_node):
		_preview_pivot.remove_child(_preview_node)
		_preview_node.queue_free()
	_preview_node = null


## Point the camera at the model and back off far enough to hold all of it.
func _frame_preview(node: Node3D) -> void:
	var bounds := AABB()
	var found := false
	for child in node.get_children():
		if child is MeshInstance3D:
			bounds = child.get_aabb() if not found else bounds.merge(child.get_aabb())
			found = true
	if node is MeshInstance3D:
		bounds = node.get_aabb()
		found = true
	if not found or bounds.size.length() <= 0.0:
		bounds = AABB(Vector3(-1, 0, -1), Vector3(2, 2, 2))

	# Centre the model on the pivot so orbiting turns it in place rather than swinging it
	# around a corner of its bounding box.
	_preview_pivot.position = -bounds.get_center()
	# A map is tens of metres and a character is under two; one fixed distance cannot suit
	# both, so it scales with what is actually there.
	_orbit_distance = maxf(bounds.size.length() * 0.85, 0.5)
	_apply_orbit()


func _apply_orbit() -> void:
	var yaw := deg_to_rad(_orbit.x)
	var pitch := deg_to_rad(clampf(_orbit.y, -85.0, 85.0))
	var offset := Vector3(
		sin(yaw) * cos(pitch),
		-sin(pitch),
		cos(yaw) * cos(pitch)) * _orbit_distance
	_preview.set_camera_pose(offset, Vector3(rad_to_deg(pitch), rad_to_deg(yaw) + 180.0, 0.0))


func _on_preview_input(event: InputEvent) -> void:
	if event is InputEventMouseMotion and (event.button_mask & MOUSE_BUTTON_MASK_LEFT):
		_orbit.x -= event.relative.x * 0.5
		_orbit.y = clampf(_orbit.y + event.relative.y * 0.5, -85.0, 85.0)
		_apply_orbit()
	elif event is InputEventMouseButton and event.pressed:
		# Wheel zoom, clamped so the model cannot be pushed inside the near plane or lost.
		if event.button_index == MOUSE_BUTTON_WHEEL_UP:
			_orbit_distance = maxf(_orbit_distance * 0.9, 0.3)
			_apply_orbit()
		elif event.button_index == MOUSE_BUTTON_WHEEL_DOWN:
			_orbit_distance = minf(_orbit_distance * 1.1, 500.0)
			_apply_orbit()


## The pose the user settled on, or "" for the importer's own choice.
func _chosen_pose() -> String:
	if _pose_row.visible and _pose_picker.selected >= 0:
		return str(_pose_picker.get_item_metadata(_pose_picker.selected))
	return ""


## Which engine badge to show beside a game in the list.
##
## Taken from what the VFS reports for the archives actually mounted, not from the display
## name: "Half-Life" and "Half-Life 2" are one character apart and different engines, and a
## folder the user browsed to may be named anything at all.
func _engine_icon(label: String) -> String:
	if not _sources.has(label):
		return "engine_source"

	var prefixes := {}
	for entry in (_sources[label] as Dictionary).get("mounts", []):
		prefixes[str((entry as Dictionary).get("prefix", ""))] = true

	for m in _vfs.get_mounts_info():
		var info: Dictionary = m
		if not prefixes.has(str(info.get("prefix", ""))):
			continue
		match str(info.get("engine", "")):
			"GoldSrc": return "engine_goldsrc"
			"Source1": return "engine_source"
			"RealVirtuality": return "engine_rv"
	return "engine_source"


## Resolve an icon: this addon's own SVG first, Godot's built-in EditorIcons as a
## fallback, null outside the editor (the headless dock test renders without icons).
##
## `name` is either one of our filenames ("type_model") or a Godot editor icon name
## ("Search"), so a caller can ask for whichever exists.
func _icon(name: String) -> Texture2D:
	if _icon_cache.has(name):
		return _icon_cache[name]

	var tex: Texture2D = _load_svg_icon(name)
	if tex == null and has_theme_icon(name, "EditorIcons"):
		tex = get_theme_icon(name, "EditorIcons")
	_icon_cache[name] = tex
	return tex


## Rasterise one of this addon's SVGs at the editor's scale, tinted to match the theme.
##
## The files use fill="currentColor", which is how Godot's own editor icons are authored —
## but that only resolves because the editor feeds them through its theme colour map.
## Loading one as an ordinary texture gives the rasteriser no CSS context, so currentColor
## comes out black: invisible on the dark theme, and unfixable with modulate afterwards
## because black multiplied by any tint stays black. Substituting the colour into the
## source before rasterising is what makes these theme-aware.
func _load_svg_icon(name: String) -> Texture2D:
	var path := ICON_DIR.path_join(name + ".svg")
	if not FileAccess.file_exists(path):
		return null

	var svg := FileAccess.get_file_as_string(path)
	if svg.is_empty():
		return null

	var tint := Color(0.88, 0.89, 0.92)
	if has_theme_color("font_color", "Editor"):
		tint = get_theme_color("font_color", "Editor")
	svg = svg.replace("currentColor", "#" + tint.to_html(false))

	var scale := 1.0
	if _plugin != null:
		scale = EditorInterface.get_editor_scale()

	var image := Image.new()
	if image.load_svg_from_string(svg, scale) != OK:
		return null
	return ImageTexture.create_from_image(image)


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
		popup.add_item(tr("No installed games found automatically"), 1000)
		popup.set_item_disabled(popup.get_item_count() - 1, true)
	else:
		for key in _detected_games.keys():
			popup.add_item(str(key), id)
			id += 1
	popup.add_separator()
	popup.add_item(tr("Choose a game folder..."), 1001)
	popup.add_item(tr("Open one archive file..."), 1002)


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
		_file_dialog.add_filter("*.vpk, *.wad, *.gma, *.pbo", tr("Game archives"))
	_file_dialog.popup_centered_ratio(0.6)


func _add_game_folder(real_dir: String, label: String) -> void:
	if _sources.has(label):
		_say(tr("%s is already in the list.") % label)
		return

	var scan: Dictionary = _vfs.scan_game_directory(real_dir)
	if scan.has("error"):
		_say(tr("Could not read that folder. Check it still exists."), true)
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
		_say(tr("Nothing importable in that folder."), true)
		return

	_sources[label] = {"path": real_dir, "mounts": mounts}
	_save_sources()
	_refresh_games()
	_refresh_results()
	_say(tr("Added %s. Search for something above.") % label)


func _add_archive(path: String) -> void:
	var label := path.get_file()
	if _sources.has(label):
		_say(tr("%s is already in the list.") % label)
		return

	var prefix := _unique_prefix(path.get_file().get_basename())
	if not _vfs.mount_container(prefix, path):
		_say(tr("%s is not a game archive Quebratsk can read.") % label, true)
		return

	_sources[label] = {
		"path": path,
		"mounts": [{"prefix": prefix, "path": path, "is_directory": false}],
	}
	_save_sources()
	_refresh_games()
	_refresh_results()
	_say(tr("Added %s.") % label)


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
		empty.set_text(0, tr("Nothing added yet"))
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
		item.set_icon(0, _icon(_engine_icon(label)))
		item.set_text(0, "%s      %s files" % [label, _grouped(files)])
		item.set_tooltip_text(0, "%s\n%d archive%s mounted"
			% [group.get("path", ""), parts, "" if parts == 1 else "s"])
		if _icon("action_remove") != null:
			item.add_button(1, _icon("action_remove"), 0, false, tr("Remove %s") % label)
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
	_say(tr("Removed %s.") % label)


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
		return tr("Picking up where you left off.")
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


func _on_search_typed(_text: String) -> void:
	_search_debounce.start()


## Which game a hit belongs to, worked out from the mount prefix in its URI.
func _game_of(uri: String) -> String:
	var rest := uri.trim_prefix("vfs://")
	var slash := rest.find("/")
	if slash < 0:
		return ""
	var prefix := rest.substr(0, slash)
	for key in _sources.keys():
		for entry in (_sources[key] as Dictionary).get("mounts", []):
			if str((entry as Dictionary).get("prefix", "")) == prefix:
				return str(key)
	return ""


func _refresh_results() -> void:
	_results.clear()
	_clear_selection()

	var root := _results.create_item()
	var needle := _search.text.strip_edges().to_lower()
	var category: Dictionary = CATEGORIES[_filter.selected]
	var wanted: Array = category["ext"]
	var folders: Array = category["folders"]

	var all: PackedStringArray = _vfs.list_files()
	if all.is_empty():
		var hint := _results.create_item(root)
		hint.set_text(0, tr("Add a game to see what is inside it"))
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

		var lower := uri.to_lower()
		if not folders.is_empty():
			var in_folder := false
			for f in folders:
				if lower.contains(str(f)):
					in_folder = true
					break
			if not in_folder:
				continue

		if not needle.is_empty() and not lower.contains(needle):
			continue
		matched += 1
		if shown >= MAX_RESULTS:
			continue

		var item := _results.create_item(root)
		var kind := _kind_of(uri)
		item.set_icon(0, _icon(str(kind["icon"])))
		# The name is what a person scans for; the full path is context and lives in the
		# tooltip, rather than the whole row being one long unreadable path.
		item.set_text(0, uri.get_file())
		item.set_text(1, "/ " + _game_of(uri))
		item.set_custom_color(1, Color(0.58, 0.62, 0.70))
		item.set_tooltip_text(0, uri)
		item.set_tooltip_text(1, tr("From %s") % _game_of(uri))
		item.set_metadata(0, uri)
		shown += 1

	if matched == 0:
		var none := _results.create_item(root)
		none.set_text(0, tr("Nothing found"))
		none.set_selectable(0, false)
		none.set_custom_color(0, Color(0.55, 0.58, 0.64))
		_say(tr("No matches. Try a shorter word, or a different category."))
	elif matched > shown:
		_say(tr("Showing the first %s of %s. Keep typing to narrow it down.")
			% [_grouped(shown), _grouped(matched)])
	else:
		# Two whole sentences rather than "item" + a conditional "s": plural rules differ
		# per language, and a translator cannot fix a word glued together in code.
		if matched == 1:
			_say(tr("%s item.") % _grouped(matched))
		else:
			_say(tr("%s items.") % _grouped(matched))


func _clear_selection() -> void:
	_selected_uri = ""
	_add_button.disabled = true
	_pose_row.visible = false
	_picked_name.text = tr("Nothing yet")
	_picked_kind.text = tr("Pick something from the list above.")
	if _preview_frame != null:
		_clear_preview()


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
	_picked_kind.text = "%s · %s · %s / %s" % [tr(str(kind["name"])),
		String.humanize_size(_vfs.get_file_size(uri)), _game_of(uri), where]

	_pose_row.visible = false
	if uri.get_extension().to_lower() == "mdl":
		_load_poses(uri)

	_clear_preview()
	_preview_debounce.start()

	if bool(kind["placeable"]):
		_add_button.disabled = false
		_say(tr("Ready. Press Add to scene."))
	else:
		_add_button.disabled = true
		var why := str(kind.get("why", ""))
		if why.is_empty():
			why = tr("%ss can be found here, but they are used by models and maps rather than placed on their own.") % tr(str(kind["name"]))
		else:
			why = tr(why)
		_say(why)


## Poses live in the .mdl and its .ani, so this skips the vertex data — but it is not
## free (~70 ms for a Garry's Mod player model). Called on selection only, never while
## typing in the search box.
func _load_poses(uri: String) -> void:
	var poses: PackedStringArray = _importer.list_poses(uri)
	_pose_picker.clear()
	if poses.is_empty():
		return
	_pose_picker.add_item(tr("Whatever the game uses by default"))
	_pose_picker.set_item_metadata(0, "")
	for p in poses:
		_pose_picker.add_item(p)
		_pose_picker.set_item_metadata(_pose_picker.item_count - 1, p)
	_pose_picker.select(0)
	_pose_row.visible = true
	# Changing the pose has to rebuild the model; that is the whole point of previewing it.
	if not _pose_picker.item_selected.is_connected(_on_pose_chosen):
		_pose_picker.item_selected.connect(_on_pose_chosen)


func _on_pose_chosen(_index: int) -> void:
	_preview_debounce.start()


# ----------------------------------------------------------------------- import ----

func _on_add_pressed() -> void:
	if _selected_uri.is_empty() or _add_button.disabled or _plugin == null:
		return

	var scene_root := _plugin.get_editor_interface().get_edited_scene_root()
	if scene_root == null:
		_say(tr("Open a scene first — then press Add to scene again."), true)
		return

	var ext := _selected_uri.get_extension().to_lower()
	var node: Node3D = null

	# Adopt what is already on screen. The preview holds a fully imported node built with
	# the pose the user settled on, so re-importing here would repeat ~170 ms of work to
	# produce exactly the same thing — and could differ from what they were looking at.
	if is_instance_valid(_preview_node):
		node = _preview_node
		_preview_pivot.remove_child(node)
		_preview_node = null
		_preview_frame.visible = false
		node.position = Vector3.ZERO
	elif ext == "bsp" or ext == "wrp":
		var mesh: ArrayMesh = _importer.load_mesh(_selected_uri)
		if mesh != null:
			var mi := MeshInstance3D.new()
			mi.mesh = mesh
			mi.name = _selected_uri.get_file().get_basename()
			node = mi
	else:
		node = _importer.load_model(_selected_uri, _chosen_pose())

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
	_say(tr("Added %s. It is selected in the scene now.") % node.name)


func _claim_ownership(node: Node, scene_root: Node) -> void:
	node.owner = scene_root
	for child in node.get_children():
		_claim_ownership(child, scene_root)


## Say what went wrong in terms of something the user can do about it.
func _explain_failure() -> String:
	match _importer.get_last_error_code():
		UnifiedAssetImporter.ERR_ASSET_UNREADABLE:
			return tr("That file could not be read. Was the game moved or uninstalled?")
		UnifiedAssetImporter.ERR_PARSE_FAILED:
			if _selected_uri.get_extension().to_lower() == "mdl":
				return tr("This model keeps its shape in companion files that are not here. Add the rest of the game's archives and try again.")
			return tr("This file was recognised but nothing could be read out of it.")
		UnifiedAssetImporter.ERR_VFS_NOT_SET:
			return tr("Something went wrong inside the plugin. Disable and re-enable it.")
		_:
			return tr("That did not work. The Output panel at the bottom has the details.")
