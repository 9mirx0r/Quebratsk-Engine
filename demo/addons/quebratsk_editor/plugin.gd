@tool
extends EditorPlugin

## Registers the Quebratsk dock.
##
## There is deliberately no EditorImportPlugin here. Godot's import pipeline operates on
## files inside res://, and game archives are 1-10 GB living in a Steam folder — nobody
## copies hl2_textures_dir.vpk into their project. The dock mounts them where they are and
## imports individual assets on demand instead.

const DockScene := preload("res://addons/quebratsk_editor/quebratsk_dock.gd")

var _dock: Control


func _enter_tree() -> void:
	_dock = DockScene.new()
	_dock.name = "Quebratsk"
	_dock.set_editor_plugin(self)
	add_control_to_dock(DOCK_SLOT_LEFT_UR, _dock)
	_reveal_once()


## Bring the dock to the front the first time it is ever installed.
##
## add_control_to_dock() appends a tab, and the left dock strip is narrow enough that
## "Quebratsk" lands past the scroll arrows behind Scene and Import — a new user installs
## the plugin and sees no evidence it did anything. Revealing it on every start would
## hide the Scene tree each time the editor opens, so this fires once and then never
## again.
func _reveal_once() -> void:
	var cfg := ConfigFile.new()
	cfg.load(DockScene.MOUNTS_FILE)
	if bool(cfg.get_value("ui", "introduced", false)):
		return

	await get_tree().process_frame
	if not is_instance_valid(_dock):
		return
	var tabs := _dock.get_parent() as TabContainer
	if tabs != null:
		tabs.current_tab = _dock.get_index()

	cfg.set_value("ui", "introduced", true)
	cfg.save(DockScene.MOUNTS_FILE)


func _exit_tree() -> void:
	if _dock:
		remove_control_from_docks(_dock)
		_dock.queue_free()
		_dock = null
