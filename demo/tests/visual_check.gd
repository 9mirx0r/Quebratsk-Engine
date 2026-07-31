extends Control

## Visual verification that runs as a game, not in the editor.
##
## Two things about this plugin can only be judged by looking: whether the 16px icons are
## distinguishable, and whether the 3D preview frames a model sensibly. Neither is
## reachable from a headless run, and driving the editor with synthetic clicks aims at
## whatever window happens to have focus — which once meant typing into a Minecraft
## session.
##
## So the dock is instantiated here as an ordinary Control. It degrades outside the editor
## by design: EditorIcons and EditorInterface are simply absent, and the addon's own SVGs
## are loaded through the very same _load_svg_icon() the editor uses, so what is rendered
## below is what a user actually gets.

const DockScript := preload("res://addons/quebratsk_editor/quebratsk_dock.gd")
const GMOD := "C:/Program Files (x86)/Steam/steamapps/common/GarrysMod/garrysmod"

const ICONS := [
	"type_model", "type_map", "type_terrain", "type_texture", "type_material",
	"type_sound", "engine_goldsrc", "engine_source", "engine_rv",
	"action_add", "action_remove",
]

var _dock: VBoxContainer


func _ready() -> void:
	DirAccess.remove_absolute(ProjectSettings.globalize_path(DockScript.MOUNTS_FILE))

	var root := HBoxContainer.new()
	root.set_anchors_preset(Control.PRESET_FULL_RECT)
	root.add_theme_constant_override("separation", 16)
	add_child(root)

	_dock = DockScript.new()
	_dock.custom_minimum_size = Vector2(330, 0)
	root.add_child(_dock)
	await get_tree().process_frame

	root.add_child(_build_icon_sheet())

	_dock._add_game_folder(GMOD, "Garry's Mod")
	_dock._search.text = "police"
	# Characters & people, so the selection lands on something placeable. Filtering by
	# "Everything" picks whatever sorts first, which was a .vmt -- browsable, not
	# previewable, and therefore a blank panel.
	for i in _dock.CATEGORIES.size():
		if str((_dock.CATEGORIES[i] as Dictionary)["label"]) == "Characters & people":
			_dock._filter.select(i)
			break
	_dock._refresh_results()

	var row: TreeItem = _dock._results.get_root().get_first_child()
	while row != null:
		if row.is_selectable(0) and str(row.get_metadata(0)).ends_with(".mdl"):
			_dock._results.set_selected(row, 0)
			_dock._on_result_selected()
			_dock._refresh_preview()
			break
		row = row.get_next()

	print("dock ready — preview node: %s"
		% ("null" if _dock._preview_node == null else _dock._preview_node.get_class()))


## The icons at the size they are actually used, plus a magnified copy, over both a dark
## and a light swatch — a monochrome icon that reads on one can vanish on the other.
func _build_icon_sheet() -> Control:
	var panel := PanelContainer.new()
	panel.custom_minimum_size = Vector2(430, 0)
	var box := VBoxContainer.new()
	box.add_theme_constant_override("separation", 2)
	panel.add_child(box)

	var title := Label.new()
	title.text = "ICONS — 16px (as used) · 48px · on light"
	title.add_theme_font_size_override("font_size", 12)
	box.add_child(title)

	for icon_name in ICONS:
		var row := HBoxContainer.new()
		row.add_theme_constant_override("separation", 12)
		box.add_child(row)

		var tex: Texture2D = _dock._load_svg_icon(str(icon_name))

		var at16 := TextureRect.new()
		at16.texture = tex
		at16.custom_minimum_size = Vector2(16, 16)
		at16.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
		row.add_child(at16)

		var at48 := TextureRect.new()
		at48.texture = tex
		at48.custom_minimum_size = Vector2(48, 48)
		at48.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
		row.add_child(at48)

		# Same texture over a light swatch: these are tinted for a dark theme, and this is
		# where an icon that is nearly white disappears.
		var light := ColorRect.new()
		light.color = Color(0.86, 0.86, 0.88)
		light.custom_minimum_size = Vector2(52, 48)
		row.add_child(light)
		var over := TextureRect.new()
		over.texture = tex
		over.set_anchors_preset(Control.PRESET_FULL_RECT)
		over.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
		light.add_child(over)

		var label := Label.new()
		label.text = "%s%s" % [icon_name, "" if tex != null else "   ** MISSING **"]
		label.add_theme_font_size_override("font_size", 12)
		row.add_child(label)

	return panel
