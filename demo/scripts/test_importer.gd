extends Node3D

# Quebratsk Engine — GDScript Ingestion Demo
var vfs: VFSManager
var importer: UnifiedAssetImporter

func _ready() -> void:
    print("[QuebratskEngine] Initializing subsystem...")
    vfs = VFSManager.new()
    importer = UnifiedAssetImporter.new()
    importer.set_vfs(vfs)

    # 1. Mount container archives
    vfs.mount_container("cs16", "res://assets/packages/cstrike.wad")
    vfs.mount_container("gmod", "res://assets/packages/weapons.gma")
    vfs.mount_container("dayz", "res://assets/packages/models.pbo")

    # 2. Check file availability
    if vfs.file_exists("vfs://gmod/models/weapons/w_rifle.mdl"):
        print("[QuebratskEngine] Found GMod rifle mesh! Importing...")
        var mesh: ArrayMesh = importer.load_mesh("vfs://gmod/models/weapons/w_rifle.mdl")
        
        var mesh_instance := MeshInstance3D.new()
        mesh_instance.mesh = mesh
        add_child(mesh_instance)
        print("[QuebratskEngine] Mesh successfully spawned in Godot 4 scene!")
