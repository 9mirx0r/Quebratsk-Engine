@tool
extends EditorPlugin

var dock: Control

func _enter_tree() -> void:
    dock = Control.new()
    dock.name = "QuebratskVFS"
    
    var label := Label.new()
    label.text = "Quebratsk VFS Explorer Dock — Active"
    dock.add_child(label)
    
    add_control_to_dock(DOCK_SLOT_LEFT_UR, dock)

func _exit_tree() -> void:
    if dock:
        remove_control_from_docks(dock)
        dock.free()
