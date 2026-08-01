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

## Reached by path rather than by global class name: a class_name lives in a cache the
## editor generates, which a fresh clone does not have until it has scanned once.
const SoundLoader := preload("res://addons/quebratsk_editor/sound_loader.gd")

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
	"wav": {"name": "Sound", "icon": "type_sound", "placeable": true},
	"mp3": {"name": "Sound", "icon": "type_sound", "placeable": true},
	"ogg": {"name": "Sound", "icon": "type_sound", "placeable": true},
}

## Never listed. These are pieces of another file rather than things in their own right:
## a Source model is split across police.mdl + police.vvd + police.dx90.vtx + police.ani,
## and the importer pulls the companions in by itself. Showing them meant a search for
## "police" returned four rows where the user wanted one, three of which do nothing when
## picked.
const COMPANION_EXTENSIONS := ["vvd", "vtx", "ani", "phy"]

## Sounds, which are placed and saved differently from geometry: there is no mesh to
## build, and what you want out of one is the audio file itself.
const SOUND_EXTENSIONS := ["wav", "mp3", "ogg"]

## What mount_container() can open, for the drag-and-drop filter.
const _MOUNTABLE := ["vpk", "wad", "gma", "pbo"]

## How many recently imported assets to remember.
const MAX_RECENTS := 12

## Ready-made categories, so a user can browse without knowing what to search for.
##
## `ext` narrows by file type; `folders` narrows by where the file sits. Both GoldSrc and
## Source lay content out by convention — models/weapons/, models/player/,
## models/props_vehicles/ — so the folder is a far better guide to what a thing IS than
## its extension, which only says "model". An empty list means "do not narrow on this".
## Two pseudo-categories sit in front of the real ones. They ignore `ext` and `folders`
## and draw from the user's own lists instead, because in 25,990 files per game the hard
## problem is not finding something once, it is finding it again.
const CATEGORY_FAVOURITES := "Favourites"
const CATEGORY_RECENT := "Recently imported"

const CATEGORIES := [
	{"label": CATEGORY_FAVOURITES, "ext": [], "folders": [], "icon": "category_favourites"},
	{"label": CATEGORY_RECENT, "ext": [], "folders": [], "icon": "category_recents"},
	{"label": "Everything", "ext": [], "folders": [], "icon": "category_all"},
	{"label": "Characters & people", "ext": ["mdl", "p3d"],
		"folders": ["models/player", "/humans/", "/npc", "/zombie", "/combine_", "/police"],
		"icon": "category_characters"},
	{"label": "Weapons", "ext": ["mdl", "p3d"],
		"folders": ["/weapons/", "/w_", "/v_", "/shells/"], "icon": "category_weapons"},
	{"label": "Vehicles", "ext": ["mdl", "p3d"],
		"folders": ["vehicle", "/cars/", "/car_", "/airboat", "/buggy", "/jeep"],
		"icon": "category_vehicles"},
	{"label": "Props & scenery", "ext": ["mdl", "p3d"],
		"folders": ["/props", "/furniture", "/gibs/"], "icon": "category_props"},
	{"label": "All models", "ext": ["mdl", "p3d"], "folders": [], "icon": "type_model"},
	{"label": "Maps & terrain", "ext": ["bsp", "wrp"], "folders": [], "icon": "type_map"},
	{"label": "Textures & materials", "ext": ["vtf", "vmt", "paa", "tga", "png"], "folders": [],
		"icon": "type_texture"},
	{"label": "Sounds", "ext": ["wav", "mp3", "ogg"], "folders": [], "icon": "type_sound"},
]

var _plugin: EditorPlugin

var _vfs: VFSManager
var _importer: UnifiedAssetImporter

var _games: Tree
var _search: LineEdit
var _filter: OptionButton
var _game_filter: OptionButton
var _results: Tree
var _picked_name: Label
var _picked_kind: Label
var _pose_row: VBoxContainer
var _pose_picker: OptionButton
var _animate_toggle: CheckBox
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

## Mount prefix -> game display name, rebuilt whenever the mounts change. See _game_of().
var _prefix_to_game: Dictionary = {}

## How many files the last search matched, before the page limit. Kept so a check can ask
## whether narrowing the search actually narrowed anything, which reading the status label
## cannot answer once it has been rounded and translated.
var _last_total := 0

## vfs:// URI -> true. A set, so membership is a hash lookup while drawing 400 rows.
var _favourites: Dictionary = {}
## Most recent first, capped at MAX_RECENTS.
var _recents: Array = []
var _star_button: Button
var _save_button: Button
var _texture_frame: TextureRect
var _empty_art: TextureRect
var _sound_row: HBoxContainer
var _play_button: Button
var _audio: AudioStreamPlayer


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
	_games.add_theme_constant_override("v_separation", 3)
	_games.hide_root = true
	_games.columns = 2
	_games.set_column_expand(1, false)
	_games.set_column_custom_minimum_width(1, 26)
	_games.button_clicked.connect(_on_remove_game)
	_games.item_activated.connect(_on_game_row_activated)
	_games.item_selected.connect(_on_game_row_activated)
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
		var entry: Dictionary = f
		_filter.add_item(tr(str(entry["label"])))
		var art := _icon(str(entry.get("icon", "")))
		if art != null:
			_filter.set_item_icon(_filter.item_count - 1, art)
	_filter.tooltip_text = tr("Browse by what a thing is, without having to search for it")
	_filter.item_selected.connect(func(_i: int) -> void: _refresh_results())
	search_box.add_child(_filter)

	# Which game to look in. With three or four games mounted, searching "police" across all
	# of them and scrolling for the one you meant is the whole problem: the origin column
	# tells you where a hit came from, but only after you have found it.
	_game_filter = OptionButton.new()
	_game_filter.tooltip_text = tr("Look in one game instead of all of them")
	_game_filter.item_selected.connect(func(_i: int) -> void: _refresh_results())
	search_box.add_child(_game_filter)
	_refresh_game_filter()

	var results_margin := MarginContainer.new()
	results_margin.add_theme_constant_override("margin_left", 4)
	results_margin.add_theme_constant_override("margin_right", 4)
	results_margin.add_theme_constant_override("margin_top", 4)
	results_margin.size_flags_vertical = Control.SIZE_EXPAND_FILL
	add_child(results_margin)

	var results_column := VBoxContainer.new()
	results_column.size_flags_vertical = Control.SIZE_EXPAND_FILL
	results_margin.add_child(results_column)

	# Shown only while nothing is mounted. Someone opening this dock for the first time
	# gets an empty list and a sentence; a picture of a crate says "there is something to
	# open here" faster than the sentence does.
	_empty_art = TextureRect.new()
	_empty_art.texture = _load_png("empty_state")
	_empty_art.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	_empty_art.custom_minimum_size = Vector2(0, 150)
	_empty_art.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_empty_art.visible = false
	if _empty_art.texture != null:
		results_column.add_child(_empty_art)

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
	# Ctrl-click and shift-click to take several. Building a street wants twenty props, and
	# doing that one round-trip at a time is the difference between usable and tedious.
	_results.select_mode = Tree.SELECT_MULTI
	_results.custom_minimum_size = Vector2(0, 170)
	_results.add_theme_constant_override("v_separation", 3)
	_results.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_results.item_selected.connect(_on_result_selected)
	_results.item_activated.connect(_on_add_pressed)
	results_column.add_child(_results)

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
	var frame_style := StyleBoxFlat.new()
	frame_style.bg_color = Color(0.09, 0.10, 0.12)
	frame_style.border_color = Color(1, 1, 1, 0.07)
	frame_style.set_border_width_all(1)
	frame_style.set_corner_radius_all(3)
	_preview_frame.add_theme_stylebox_override("panel", frame_style)
	_preview_frame.gui_input.connect(_on_preview_input)

	# The viewport is transparent, so without this the model floats on flat panel colour and
	# reads as a hole rather than as a space. A tiled grid gives it a floor to sit against.
	var backdrop := TextureRect.new()
	backdrop.texture = _load_png("preview_backdrop")
	backdrop.stretch_mode = TextureRect.STRETCH_TILE
	backdrop.set_anchors_preset(Control.PRESET_FULL_RECT)
	backdrop.mouse_filter = Control.MOUSE_FILTER_IGNORE
	if backdrop.texture != null:
		_preview_frame.add_child(backdrop)

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

	# Textures get the same slot as the 3D view. Only one of the two is ever shown, so a
	# .vtf reads as "here is your thing" rather than an empty panel with a name over it.
	_texture_frame = TextureRect.new()
	_texture_frame.custom_minimum_size = Vector2(0, 170)
	_texture_frame.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	_texture_frame.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	_texture_frame.visible = false
	picked.add_child(_texture_frame)

	_sound_row = HBoxContainer.new()
	_sound_row.visible = false
	picked.add_child(_sound_row)
	_play_button = Button.new()
	_play_button.text = tr("Play")
	_play_button.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_play_button.pressed.connect(_on_play_pressed)
	_sound_row.add_child(_play_button)

	_audio = AudioStreamPlayer.new()
	add_child(_audio)

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

	# A pose is one frame of a sequence; this brings in the whole sequence as something
	# that plays. It is off by default because it costs real time and memory — a nine
	# second idle is 280 keyframes per moving bone — and most imports want a stance.
	_animate_toggle = CheckBox.new()
	_animate_toggle.text = tr("Bring it in moving")
	_animate_toggle.tooltip_text = tr("Import the whole sequence as an animation you can play, not just the one frame")
	_animate_toggle.toggled.connect(_on_animate_toggled)
	_pose_row.add_child(_animate_toggle)

	var button_row := _row()

	_star_button = Button.new()
	_star_button.text = "☆"
	_star_button.tooltip_text = tr("Pin this so it stays easy to find")
	_star_button.disabled = true
	_star_button.pressed.connect(_on_star_pressed)
	button_row.add_child(_star_button)

	_save_button = Button.new()
	_save_button.text = tr("Save")
	_save_button.tooltip_text = tr("Write it to res://imported/ so you can reuse it")
	_save_button.disabled = true
	_save_button.pressed.connect(_on_save_pressed)
	button_row.add_child(_save_button)

	_add_button = Button.new()
	_add_button.text = tr("Add to scene")
	_add_button.icon = _icon("action_add")
	_add_button.disabled = true
	# The primary action, and the only one that should read as such. Star and Save sit
	# beside it at their natural width so the eye lands here first.
	_add_button.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_add_button.size_flags_stretch_ratio = 3.0
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
	var ext_now := _selected_uri.get_extension().to_lower()

	# Not placeable does not mean not previewable. A texture can be shown and a sound can
	# be played; both were listed but inert before.
	if str(kind["name"]) == tr("Texture") or ext_now in ["vtf", "png", "tga", "paa"]:
		_preview_texture(_selected_uri)
		return
	if SOUND_EXTENSIONS.has(ext_now):
		_sound_row.visible = true
		return
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
		node = _importer.load_model(_selected_uri, _chosen_pose(), _chosen_animations())

	if node == null:
		return

	_preview_node = node
	_preview_pivot.add_child(node)
	_frame_preview(node)
	_preview_frame.visible = true


## Show a texture in the preview slot. Covers .vtf through the engine's own decoder as
## well as the plain .png and .tga that mods drop next to their models.
func _preview_texture(uri: String) -> bool:
	var tex: Texture2D = _importer.load_texture(uri)
	if tex == null:
		# The checkerboard every engine in this list uses for a texture it could not find.
		# Showing it says "this exists and could not be read", which an empty frame does not.
		var stand_in := _load_png("missing_texture")
		if stand_in == null:
			return false
		_texture_frame.texture = stand_in
		_texture_frame.visible = true
		return true
	_texture_frame.texture = tex
	_texture_frame.visible = true
	_picked_kind.text += "  ·  %d x %d" % [tex.get_width(), tex.get_height()]
	return true


## Build a playable stream from a .wav in the VFS.
##
## Godot can only load audio off disk, and these files live inside a VPK that is never
## extracted, so the container is parsed here and the samples handed over directly. RIFF
## is a chunk list, not a fixed header: the fmt and data chunks are found by walking it,
## because real game audio carries LIST and fact chunks in between.
## Any sound the dock lists, as something Godot can play. The RIFF walk lives in
## sound_loader.gd so the dock and anything built on the importer share one copy of it.
func _load_sound(uri: String) -> AudioStream:
	return SoundLoader.load_sound(_vfs, uri)


func _load_wav(uri: String) -> AudioStreamWAV:
	return SoundLoader.load_wav(_vfs, uri)


func _on_play_pressed() -> void:
	if _audio.playing:
		_audio.stop()
		_play_button.text = tr("Play")
		return
	var stream := _load_sound(_selected_uri)
	if stream == null:
		_say(tr("This sound is in a format Quebratsk cannot play yet."), true)
		return
	_audio.stream = stream
	_audio.play()
	_play_button.text = tr("Stop")


func _clear_preview() -> void:
	_preview_frame.visible = false
	_texture_frame.visible = false
	_texture_frame.texture = null
	_sound_row.visible = false
	if _audio != null and _audio.playing:
		_audio.stop()
	if _play_button != null:
		_play_button.text = tr("Play")
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
	#
	# The factor is set against Godot's default 75 degree vertical FOV: filling the frame
	# with an object of height h needs (h / 2) / tan(37.5 deg), about 0.65 h. Using the
	# box diagonal instead of the height keeps wide objects in frame too.
	_orbit_distance = maxf(bounds.size.length() * 0.62, 0.4)
	_apply_orbit()


func _apply_orbit() -> void:
	var yaw := deg_to_rad(_orbit.x)
	var pitch := deg_to_rad(clampf(_orbit.y, -85.0, 85.0))
	var offset := Vector3(
		sin(yaw) * cos(pitch),
		-sin(pitch),
		cos(yaw) * cos(pitch)) * _orbit_distance
	# A Godot camera looks down its own -Z, so a camera sitting at P and facing the origin
	# needs yaw = atan2(P.x, P.z) exactly. Adding 180 to that turns it around and leaves
	# the model behind the camera, which renders as an empty viewport rather than an error.
	_preview.set_camera_pose(offset, Vector3(rad_to_deg(pitch), rad_to_deg(yaw), 0.0))


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


## The sequences to import as playable animation, which is either the chosen one or none.
##
## "Whatever the game uses by default" names no sequence, so there is nothing to animate:
## the toggle needs a pose picked before it means anything.
func _chosen_animations() -> PackedStringArray:
	if _animate_toggle == null or not _animate_toggle.button_pressed:
		return PackedStringArray()
	var pose := _chosen_pose()
	if pose.is_empty():
		return PackedStringArray()
	return PackedStringArray([pose])


func _on_animate_toggled(pressed: bool) -> void:
	if pressed and _chosen_pose().is_empty():
		_say(tr("Pick a pose first — that is the sequence that will play."))
		return
	_preview_debounce.start()


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
## Icons are rasterised with the theme's colour baked in, so a cached one is only valid
## for the theme it was built under. Switching the editor from dark to light would
## otherwise leave every icon tinted for the old one — and near-invisible.
func _notification(what: int) -> void:
	if what == NOTIFICATION_THEME_CHANGED and not _icon_cache.is_empty():
		_icon_cache.clear()
		if _games != null:
			_refresh_games()
			_refresh_results()


func _icon(name: String) -> Texture2D:
	if _icon_cache.has(name):
		return _icon_cache[name]

	var tex: Texture2D = _load_svg_icon(name)
	if tex == null and has_theme_icon(name, "EditorIcons"):
		tex = get_theme_icon(name, "EditorIcons")
	_icon_cache[name] = tex
	return tex


## A full-colour PNG from the icons folder, cached like the SVGs are. These do not go
## through the theme colour map, so they are loaded as ordinary images.
func _load_png(name: String) -> Texture2D:
	if name.is_empty():
		return null
	var key := "png:" + name
	if _icon_cache.has(key):
		return _icon_cache[key]

	var path := ICON_DIR.path_join(name + ".png")
	var tex: Texture2D = null
	if FileAccess.file_exists(path):
		var image := Image.new()
		if image.load(path) == OK:
			tex = ImageTexture.create_from_image(image)
	_icon_cache[key] = tex
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
	_rebuild_prefix_map()
	_refresh_game_filter()
	_games.clear()
	var root := _games.create_item()

	if _sources.is_empty():
		# An empty dock with a single grey line is a dead end. The games are already known
		# by this point, so offer them as rows the user can click instead of making them
		# discover the menu first. Idea one was a bundled sample, but this project ships no
		# game content by design, so the shortest honest path is one click to a real game.
		_detected_games = SteamLibraryDetector.detect_installed_games()
		if _detected_games.is_empty():
			var empty := _games.create_item(root)
			empty.set_text(0, tr("Nothing added yet"))
			empty.set_selectable(0, false)
			empty.set_custom_color(0, Color(0.55, 0.58, 0.64))
			return

		var hint := _games.create_item(root)
		hint.set_text(0, tr("Found on this computer — click to add"))
		hint.set_selectable(0, false)
		hint.set_custom_color(0, Color(0.55, 0.58, 0.64))

		for key in _detected_games.keys():
			var label := str(key)
			var row := _games.create_item(root)
			row.set_icon(0, _icon("action_add"))
			row.set_text(0, label)
			row.set_tooltip_text(0, str(_detected_games[label]))
			row.set_metadata(0, "+" + label)  # '+' marks a suggestion, not a live mount
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


## Clicking one of the suggested games in the empty state adds it.
##
## Only rows tagged with a leading '+' are suggestions; a real mount's metadata is its
## display name, and selecting one of those must not re-add it.
func _on_game_row_activated() -> void:
	var item := _games.get_selected()
	if item == null:
		return
	var meta := str(item.get_metadata(0))
	if not meta.begins_with("+"):
		return
	var label := meta.substr(1)
	if not _detected_games.has(label):
		return
	_add_game_folder(str(_detected_games[label]), label)


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
	cfg.set_value("library", "favourites", _favourites.keys())
	cfg.set_value("library", "recents", _recents)
	cfg.save(MOUNTS_FILE)


## Returns the line to greet the user with, or "" when there was nothing to restore.
func _restore_sources() -> String:
	var cfg := ConfigFile.new()
	if cfg.load(MOUNTS_FILE) != OK:
		return ""

	# Stored as a list and rebuilt into a set: ConfigFile round-trips arrays cleanly, and
	# the set is what makes the per-row lookup while drawing 400 results a hash hit.
	for uri in cfg.get_value("library", "favourites", []):
		_favourites[str(uri)] = true
	for uri in cfg.get_value("library", "recents", []):
		_recents.append(str(uri))

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


# ------------------------------------------------------------------ drag & drop ----

## Accept archives dragged from the file manager onto the panel.
##
## Godot hands external files as {"type": "files", "files": [...]}. VFSDropHandler already
## knew how to mount those and had no caller; this is it.
func _can_drop_data(_at: Vector2, data: Variant) -> bool:
	if typeof(data) != TYPE_DICTIONARY:
		return false
	var payload: Dictionary = data
	if str(payload.get("type", "")) != "files":
		return false
	for f in payload.get("files", []):
		if _MOUNTABLE.has(str(f).get_extension().to_lower()):
			return true
	return false


func _drop_data(_at: Vector2, data: Variant) -> void:
	var files: Array = (data as Dictionary).get("files", [])
	var added := 0
	for f in files:
		var path := str(f)
		if not _MOUNTABLE.has(path.get_extension().to_lower()):
			continue
		# A _dir.vpk pulls in its own side archives, so dropping a whole folder's worth
		# would mount each numbered part as its own source.
		if path.get_extension().to_lower() == "vpk" \
				and not path.get_file().to_lower().ends_with("_dir.vpk"):
			continue
		var before := _sources.size()
		_add_archive(path)
		if _sources.size() > before:
			added += 1

	if added == 0 and not files.is_empty():
		_say(tr("Nothing there Quebratsk can read."), true)


## Which game a hit belongs to, worked out from the mount prefix in its URI.
## Rebuild the prefix lookup. Called whenever _sources changes, which is the only thing
## that can invalidate it.
func _rebuild_prefix_map() -> void:
	_prefix_to_game.clear()
	for key in _sources.keys():
		for entry in (_sources[key] as Dictionary).get("mounts", []):
			_prefix_to_game[str((entry as Dictionary).get("prefix", ""))] = str(key)


## Rebuild the game dropdown from what is mounted, keeping the current choice if that game
## is still there. Called from _refresh_games(), which is where _sources changes.
func _refresh_game_filter() -> void:
	if _game_filter == null:
		return
	var previous := ""
	if _game_filter.selected > 0 and _game_filter.selected < _game_filter.item_count:
		previous = str(_game_filter.get_item_metadata(_game_filter.selected))


	_game_filter.clear()
	_game_filter.add_item(tr("All games"))
	_game_filter.set_item_metadata(0, {})

	# What the games say they are, rather than what mounting them happened to be called.
	# Steam calls one folder "Half-Life" and there are four games inside it: Half-Life,
	# Counter-Strike, Condition Zero and its Deleted Scenes. Listing the mount offered all
	# four at once under one name, with no way to ask for just the one you meant.
	var order: Dictionary = _vfs.get_game_search_order()

	# A title is not always unique. Episode One and Episode Two both call themselves
	# HALF-LIFE 2, so where two agree, the folder is what tells them apart.
	var seen_titles := {}
	for dir in order:
		var title := str((order[dir] as Dictionary)["name"])
		seen_titles[title] = int(seen_titles.get(title, 0)) + 1

	var rows := []
	for dir in order:
		var entry: Dictionary = order[dir]
		var title := str(entry["name"])
		if int(seen_titles.get(title, 0)) > 1:
			title = "%s (%s)" % [title, str(entry["folder"])]
		rows.append({"label": title, "meta": {"game": str(dir)}})

	# Anything mounted that belongs to no game keeps an entry of its own: a folder of
	# extracted assets or a downloaded map bundle has no manifest and would otherwise be
	# reachable only under "All games".
	for key in _sources.keys():
		if _game_has_manifest(key, order):
			continue
		rows.append({"label": str(key), "meta": {"source": str(key)}})

	rows.sort_custom(func(a: Dictionary, b: Dictionary) -> bool:
		return str(a["label"]).naturalnocasecmp_to(str(b["label"])) < 0)

	for row in rows:
		_game_filter.add_item(str((row as Dictionary)["label"]))
		var at := _game_filter.item_count - 1
		_game_filter.set_item_metadata(at, (row as Dictionary)["meta"])
		if str((row as Dictionary)["meta"]) == previous:
			_game_filter.select(at)

	# One game is no choice at all, and a dropdown with a single real entry is clutter.
	_game_filter.visible = rows.size() > 1


## Is this source already represented by the games in the list?
##
## A game the engine recognised is in the list under its own name, so listing the mount as
## well offers the same content twice under two labels. Steam's Half-Life folder held four
## games and its own entry, so "Half-Life" appeared twice and meant something different each
## time.
##
## Answered by whether ANY of its files landed in a game, not all of them. A folder that holds
## games also holds odds and ends outside them, and one unlucky sample used to be enough to
## conclude the whole mount belonged nowhere. Sources where nothing at all has a game keep
## their entry: an extracted asset folder or a downloaded map bundle has no manifest and would
## otherwise be reachable only under "All games".
func _game_has_manifest(source_name: String, _order: Dictionary) -> bool:
	for entry in (_sources.get(source_name, {}) as Dictionary).get("mounts", []):
		var prefix := str((entry as Dictionary).get("prefix", ""))
		var hit: Dictionary = _vfs.find_files("", PackedStringArray(), PackedStringArray(),
			PackedStringArray([prefix]), 24)
		for f in hit["files"]:
			if not _vfs.get_game_of(str(f)).is_empty():
				return true
	return false


## What the game dropdown is pointing at, as a Dictionary the search can be given.
##
## Two kinds of answer, because the two do not line up. A game is a game wherever its files
## were mounted from, and one mount can hold several. Anything with no manifest can still be
## named by the mounts it brought.
func _chosen_scope() -> Dictionary:
	var empty := {"games": PackedStringArray(), "prefixes": PackedStringArray()}
	if _game_filter == null or _game_filter.selected <= 0:
		return empty

	var meta = _game_filter.get_item_metadata(_game_filter.selected)
	if not (meta is Dictionary):
		return empty

	if (meta as Dictionary).has("game"):
		return {
			"games": PackedStringArray([str((meta as Dictionary)["game"])]),
			"prefixes": PackedStringArray(),
		}

	var source := str((meta as Dictionary).get("source", ""))
	if source.is_empty() or not _sources.has(source):
		return empty

	var prefixes := PackedStringArray()
	for entry in (_sources[source] as Dictionary).get("mounts", []):
		prefixes.append(str((entry as Dictionary).get("prefix", "")))
	return {"games": PackedStringArray(), "prefixes": prefixes}


## Which game a URI belongs to, from the mount prefix.
##
## This used to walk _sources and its nested mount arrays on every call, once per result
## row. Drawing 400 rows meant 400 nested scans of the same unchanging data. The map is
## built once when the mounts change instead.
func _game_of(uri: String) -> String:
	var rest := uri.trim_prefix("vfs://")
	var slash := rest.find("/")
	if slash < 0:
		return ""
	return str(_prefix_to_game.get(rest.substr(0, slash), ""))


func _refresh_results() -> void:
	_results.clear()
	_clear_selection()

	var root := _results.create_item()
	var needle := _search.text.strip_edges().to_lower()
	var category: Dictionary = CATEGORIES[_filter.selected]
	var wanted: Array = category["ext"]
	var folders: Array = category["folders"]

	# _sources is the dock's own record of what is mounted, so "has the user added anything
	# yet" is answered without touching the index at all. Asking the VFS meant a full scan
	# of every indexed URI on each keystroke, to learn something already known here.
	if _empty_art != null:
		_empty_art.visible = _sources.is_empty()

	if _sources.is_empty():
		var hint := _results.create_item(root)
		hint.set_text(0, tr("Add a game to see what is inside it"))
		hint.set_selectable(0, false)
		hint.set_custom_color(0, Color(0.55, 0.58, 0.64))
		return

	# The two curated categories replace the scan entirely: they are already the list.
	var label := str(category["label"])
	if label == CATEGORY_FAVOURITES or label == CATEGORY_RECENT:
		var picks: Array = _recents if label == CATEGORY_RECENT else _favourites.keys()
		var listed := 0
		for entry in picks:
			var uri := str(entry)
			# A game can be unmounted after something was starred.
			if not _vfs.file_exists(uri):
				continue
			if not needle.is_empty() and not uri.to_lower().contains(needle):
				continue
			_add_result_row(root, uri)
			listed += 1
		if listed == 0:
			var empty := _results.create_item(root)
			empty.set_selectable(0, false)
			empty.set_custom_color(0, Color(0.55, 0.58, 0.64))
			if label == CATEGORY_FAVOURITES:
				empty.set_text(0, tr("Star something to keep it here"))
				_say(tr("Pick an asset and press the star to pin it."))
			else:
				empty.set_text(0, tr("Nothing imported yet"))
				_say(tr("What you import will show up here."))
		else:
			_say(tr("%s items.") % _grouped(listed))
		return

	# Type and text filtering happen inside the engine, over the index itself, so the
	# 60,584 URIs are never copied into GDScript just to be discarded. Only the folder
	# rule runs here, and only over what already survived the cheap filters.
	var ext_filter := PackedStringArray(wanted)
	var drop := PackedStringArray(COMPANION_EXTENSIONS)
	# With a folder rule the engine cannot apply the limit, because whether a URI survives
	# is decided here. Without one its count and its page are both final.
	var hard_limit := MAX_RESULTS if folders.is_empty() else 0
	var scope := _chosen_scope()
	var found: Dictionary = _vfs.find_files(needle, ext_filter, drop, scope["prefixes"],
		hard_limit, scope["games"])

	var shown := 0
	var matched := int(found["total"])
	_last_total = matched

	for entry in found["files"]:
		var uri := str(entry)
		if not folders.is_empty():
			var lower := uri.to_lower()
			var in_folder := false
			for f in folders:
				if lower.contains(str(f)):
					in_folder = true
					break
			if not in_folder:
				matched -= 1
				continue

		if shown >= MAX_RESULTS:
			continue
		_add_result_row(root, uri)
		shown += 1

	# With a folder rule the engine could not apply the limit, so `shown` is the honest
	# ceiling and `matched` counts everything that passed.
	if not folders.is_empty():
		matched = maxi(matched, shown)
		_last_total = matched

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


func _on_star_pressed() -> void:
	if _selected_uri.is_empty():
		return
	if _favourites.has(_selected_uri):
		_favourites.erase(_selected_uri)
		_say(tr("Unpinned %s.") % _selected_uri.get_file())
	else:
		_favourites[_selected_uri] = true
		_say(tr("Pinned %s.") % _selected_uri.get_file())
	_save_sources()
	_sync_star()
	_refresh_results_keeping_selection()


func _sync_star() -> void:
	var pinned := _favourites.has(_selected_uri)
	_star_button.disabled = _selected_uri.is_empty()
	_star_button.text = "★" if pinned else "☆"


## Remember what was imported, most recent first and without duplicates.
func _remember_recent(uri: String) -> void:
	_recents.erase(uri)
	_recents.push_front(uri)
	while _recents.size() > MAX_RECENTS:
		_recents.pop_back()
	_save_sources()


## Redraw the list and put the cursor back where it was. Toggling a star changes a row's
## text, and rebuilding the tree would otherwise drop the selection under the user.
func _refresh_results_keeping_selection() -> void:
	var keep := _selected_uri
	_refresh_results()
	if keep.is_empty():
		return
	var row: TreeItem = _results.get_root().get_first_child()
	while row != null:
		if str(row.get_metadata(0)) == keep:
			_results.set_selected(row, 0)
			_on_result_selected()
			return
		row = row.get_next()


## One result row. Shared by the scan and by the curated categories so a favourite looks
## exactly like the same file found by searching.
func _add_result_row(root: TreeItem, uri: String) -> void:
	var item := _results.create_item(root)
	var kind := _kind_of(uri)
	item.set_icon(0, _icon(str(kind["icon"])))
	# The name is what a person scans for; the full path is context and lives in the
	# tooltip, rather than the whole row being one long unreadable path.
	var starred := "★ " if _favourites.has(uri) else ""
	item.set_text(0, starred + uri.get_file())
	var game := _game_of(uri)
	item.set_text(1, "/ " + game)
	item.set_custom_color(1, Color(0.58, 0.62, 0.70))
	item.set_tooltip_text(0, uri)
	item.set_tooltip_text(1, tr("From %s") % game)
	item.set_metadata(0, uri)


func _clear_selection() -> void:
	_selected_uri = ""
	_add_button.disabled = true
	if _save_button != null:
		_save_button.disabled = true
	_pose_row.visible = false
	_picked_name.text = tr("Nothing yet")
	_picked_kind.text = tr("Pick something from the list above.")
	if _preview_frame != null:
		_clear_preview()
	if _star_button != null:
		_sync_star()


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
	_sync_star()

	if bool(kind["placeable"]):
		_add_button.disabled = false
		_save_button.disabled = false
		_say(tr("Ready. Press Add to scene."))
	else:
		_add_button.disabled = true
		_save_button.disabled = true
		var why := str(kind.get("why", ""))
		if why.is_empty():
			why = tr("%ss can be found here, but they are used by models and maps rather than placed on their own.") % tr(str(kind["name"]))
		else:
			why = tr(why)
		_say(why)


## Poses live in the .mdl and its .ani, so this skips the vertex data — but it is not free
## (~7 ms for a Garry's Mod player model, nearly all of it reading the 7.1 MB animation
## model the stances are borrowed from). Called on selection only, never while typing in
## the search box.
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

const SAVE_DIR := "res://imported"

## Write the selection to res://imported/<name>.tscn.
##
## Adding to the current scene is a one-off; a saved scene can be instanced anywhere,
## versioned, and edited. This is the difference between a browser and something you build
## a level with.
func _on_save_pressed() -> void:
	var batch := _selected_uris()
	if batch.is_empty():
		return

	DirAccess.make_dir_recursive_absolute(SAVE_DIR)

	var written := 0
	var last := ""
	for entry in batch:
		var uri := str(entry)

		if SOUND_EXTENSIONS.has(uri.get_extension().to_lower()):
			var audio := _save_sound(uri)
			if not audio.is_empty():
				written += 1
				last = audio
				_remember_recent(uri)
			continue

		var node := _build_for_scene(uri, batch.size() == 1)
		if node == null:
			continue

		# PackedScene only keeps descendants owned by the node being packed, so ownership
		# is assigned against the branch root rather than an edited scene.
		_claim_ownership(node, node)
		node.owner = null

		var packed := PackedScene.new()
		if packed.pack(node) != OK:
			node.queue_free()
			continue

		var path := "%s/%s.tscn" % [SAVE_DIR, uri.get_file().get_basename().validate_filename()]
		if ResourceSaver.save(packed, path) == OK:
			written += 1
			last = path
		node.queue_free()
		_remember_recent(uri)

	if written == 0:
		_say(tr("Nothing could be saved."), true)
		return

	if _plugin != null:
		_plugin.get_editor_interface().get_resource_filesystem().scan()
	if written == 1:
		_say(tr("Saved to %s") % last)
	else:
		_say(tr("Saved %s scenes to %s/") % [_grouped(written), SAVE_DIR])


## Every placeable URI the user has selected, in tree order.
func _selected_uris() -> Array:
	var out := []
	var row: TreeItem = _results.get_next_selected(null)
	while row != null:
		var uri := str(row.get_metadata(0))
		if not uri.is_empty() and bool(_kind_of(uri)["placeable"]):
			out.append(uri)
		row = _results.get_next_selected(row)
	if out.is_empty() and not _selected_uri.is_empty():
		out.append(_selected_uri)
	return out


func _on_add_pressed() -> void:
	if _selected_uri.is_empty() or _add_button.disabled or _plugin == null:
		return

	var scene_root := _plugin.get_editor_interface().get_edited_scene_root()
	if scene_root == null:
		_say(tr("Open a scene first — then press Add to scene again."), true)
		return

	# More than one selected: import them all in one undoable action rather than making
	# the user repeat the whole pick-and-press cycle per prop.
	var batch := _selected_uris()
	if batch.size() > 1:
		_add_batch(batch, scene_root)
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
	elif SOUND_EXTENSIONS.has(ext):
		node = _build_sound_player(_selected_uri)
	elif ext == "bsp" or ext == "wrp":
		var mesh: ArrayMesh = _importer.load_mesh(_selected_uri)
		if mesh != null:
			var mi := MeshInstance3D.new()
			mi.mesh = mesh
			mi.name = _selected_uri.get_file().get_basename()
			node = mi
	else:
		node = _importer.load_model(_selected_uri, _chosen_pose(), _chosen_animations())

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

	_remember_recent(_selected_uri)
	_plugin.get_editor_interface().get_selection().clear()
	_plugin.get_editor_interface().get_selection().add_node(node)
	_say(tr("Added %s. It is selected in the scene now.") % node.name)


## Import a whole selection as one undoable step.
##
## The previewed node is not reused here: it holds only one of the picks, and adopting it
## for that one while importing the rest would make a single Ctrl+Z leave the scene half
## populated. One action, all nodes, one undo.
func _add_batch(uris: Array, scene_root: Node) -> void:
	var undo := _plugin.get_undo_redo()
	undo.create_action(tr("Add %s items") % _grouped(uris.size()))

	var added := 0
	var failed := 0
	var built: Array[Node3D] = []
	for entry in uris:
		var uri := str(entry)
		# Never `alone`: a batch is several models, and the pose picked against one of them
		# names a sequence the others most likely do not have.
		var node := _build_for_scene(uri, false)
		if node == null:
			failed += 1
			continue
		# Spaced out along X so a batch does not arrive as one pile at the origin.
		node.position = Vector3(added * 2.0, 0, 0)
		undo.add_do_method(scene_root, "add_child", node, true)
		undo.add_do_reference(node)
		undo.add_undo_method(scene_root, "remove_child", node)
		built.append(node)
		_remember_recent(uri)
		added += 1

	if added == 0:
		undo.commit_action()
		_say(tr("Nothing in that selection could be imported."), true)
		return

	undo.commit_action()
	for node in built:
		_claim_ownership(node, scene_root)

	if failed == 0:
		_say(tr("Added %s items to the scene.") % _grouped(added))
	else:
		_say(tr("Added %s of %s. The rest could not be read.")
			% [_grouped(added), _grouped(added + failed)])


## Import one URI into a scene-ready node, without touching the preview.
## Build a node for saving. `alone` is true when this is the only asset being saved, which
## is the only case where the pose and animation choices apply: they were made against the
## selected model, and a sequence label from one model rarely exists in another. Saving a
## multi-selection uses each model's own default.
func _build_for_scene(uri: String, alone: bool) -> Node3D:
	var ext := uri.get_extension().to_lower()
	if SOUND_EXTENSIONS.has(ext):
		return _build_sound_player(uri)
	if ext == "bsp" or ext == "wrp":
		var mesh: ArrayMesh = _importer.load_mesh(uri)
		if mesh == null:
			return null
		var mi := MeshInstance3D.new()
		mi.mesh = mesh
		mi.name = uri.get_file().get_basename()
		return mi
	if not alone:
		return _importer.load_model(uri, "")
	return _importer.load_model(uri, _chosen_pose(), _chosen_animations())


## A sound as something already positioned in the world.
##
## AudioStreamPlayer3D rather than the plain one: a footstep or a door belongs at a place,
## and a 2D player would have to be swapped out the moment it is attached to anything.
func _build_sound_player(uri: String) -> Node3D:
	var stream := _load_sound(uri)
	if stream == null:
		return null
	var player := AudioStreamPlayer3D.new()
	player.stream = stream
	player.name = uri.get_file().get_basename().validate_node_name()
	return player


## Write a sound into the project as the file it already is.
##
## No decoding and no re-encoding: the bytes go across untouched and Godot imports them with
## its own importer, which is both lossless and the format the rest of the project expects.
## Returns the path written, or an empty string.
func _save_sound(uri: String) -> String:
	var bytes := _vfs.read_file(uri)
	if bytes.is_empty():
		return ""
	var path := "%s/%s" % [SAVE_DIR, uri.get_file().validate_filename()]
	var file := FileAccess.open(path, FileAccess.WRITE)
	if file == null:
		return ""
	file.store_buffer(bytes)
	file.close()
	return path


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
			# Name the file rather than describing the category of problem. "Missing
			# police.vvd" is something a user can search for; "companion files" is not.
			var missing: PackedStringArray = _importer.get_last_missing_companions()
			if not missing.is_empty():
				return tr("Missing %s. That file lives in another of the game's archives — add it and try again.") \
					% ", ".join(missing)
			if _selected_uri.get_extension().to_lower() == "mdl":
				return tr("This model keeps its shape in companion files that are not here. Add the rest of the game's archives and try again.")
			return tr("This file was recognised but nothing could be read out of it.")
		UnifiedAssetImporter.ERR_VFS_NOT_SET:
			return tr("Something went wrong inside the plugin. Disable and re-enable it.")
		_:
			return tr("That did not work. The Output panel at the bottom has the details.")
