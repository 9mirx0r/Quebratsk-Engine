# 🔰 Quebratsk Engine — Beginner's Guide for Godot 4

Welcome! This step-by-step tutorial will show you how to use **Quebratsk Engine** in your Godot 4 projects, even if you are brand new to game development or GDScript.

---

## 📌 Step 1: Install Quebratsk Engine in Your Godot Project

1. Download **`quebratsk-engine-v0.1.0-alpha-godot4.zip`** from the [Releases](https://github.com/9mirx0r/Quebratsk-Engine/releases) section on GitHub.
2. Extract the `addons/quebratsk` folder into your Godot project's root folder (`res://addons/quebratsk`).
3. Re-open your Godot 4 editor. Godot will automatically detect and load the GDExtension library!

```
YourGodotProject/
├── addons/
│   └── quebratsk/
│       └── bin/
│           └── quebratsk.gdextension
└── main.tscn
```

---

## 📦 Step 2: How to Mount Your Game Archives (VFS)

Before importing assets, you tell Quebratsk where your game archive files (`.wad`, `.gma`, `.pbo`, `.pak`, `.bundle`) are located using virtual prefixes (`vfs://`):

```gdscript
extends Node3D

var vfs: VFSManager

func _ready() -> void:
    # Create the Virtual File System
    vfs = VFSManager.new()

    # Mount your game archives (Prefix, Path to File)
    vfs.mount_container("cs16", "res://assets/cstrike.wad")
    vfs.mount_container("gmod", "res://assets/weapon_pack.gma")
    vfs.mount_container("dayz", "res://assets/weapons.pbo")
    vfs.mount_container("cs2",  "res://assets/pak01_dir.vpk")
    vfs.mount_container("tarkov", "res://assets/item_bundle.bundle")
```

---

## 🎨 Step 3: How to Import and Spawn 3D Models

To load a 3D model (Half-Life `.mdl`, Counter-Strike 2 `.vmdl_c`, Arma `.p3d` or `.xob`), use the `load_mesh` method:

```gdscript
extends Node3D

var vfs: VFSManager
var importer: UnifiedAssetImporter

func _ready() -> void:
    vfs = VFSManager.new()
    importer = UnifiedAssetImporter.new()
    importer.set_vfs(vfs)

    # Mount Garry's Mod package
    vfs.mount_container("gmod", "res://assets/weapons.gma")

    # Load 3D model into an ArrayMesh
    var my_mesh: ArrayMesh = importer.load_mesh("vfs://gmod/models/weapons/w_snip_awp.mdl")

    # Create a 3D node in Godot and show it on screen
    var mesh_instance := MeshInstance3D.new()
    mesh_instance.mesh = my_mesh
    add_child(mesh_instance) # Spawns the model in your 3D scene!
```

---

## 🖼️ Step 4: How to Import Materials

To load material parameters (Source `.vmt` or Bohemia `.rvmat`):

```gdscript
var my_material: StandardMaterial3D = importer.load_material("vfs://gmod/materials/models/weapons/v_awp.vmt")
mesh_instance.material_override = my_material
```

---

## ⛰️ Step 5: How to Import Terrain Heightmaps

To load terrain heightmaps from Bohemia `.wrp` maps into a Godot 4 `CollisionShape3D`:

```gdscript
var heightmap_shape: HeightMapShape3D = importer.load_terrain("vfs://dayz/chernarus.wrp")

var collision_node := CollisionShape3D.new()
collision_node.shape = heightmap_shape
$StaticBody3D.add_child(collision_node) # Creates 3D physics terrain!
```

---

## 💡 Summary of Easy Commands

| Goal | GDScript Command |
| :--- | :--- |
| **Mount Archive** | `vfs.mount_container("prefix", "res://path/to/archive.gma")` |
| **Load 3D Model** | `importer.load_mesh("vfs://prefix/path/to/model.mdl")` |
| **Load Material** | `importer.load_material("vfs://prefix/path/to/material.vmt")` |
| **Load Terrain** | `importer.load_terrain("vfs://prefix/path/to/map.wrp")` |

---

Need help or found a bug? Join the discussion on our [GitHub Issues](https://github.com/9mirx0r/Quebratsk-Engine/issues) page!
