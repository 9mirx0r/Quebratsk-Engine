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


func _exit_tree() -> void:
	if _dock:
		remove_control_from_docks(_dock)
		_dock.queue_free()
		_dock = null
