# Changelog

All notable changes to **Quebratsk Engine** are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [Unreleased]

### Added
- **Batch GLTF Converter (`src/converters/batch_gltf_converter.h/.cpp`)**: Mass container asset library exporter converting mounted VFS archives to standalone `.gltf` files.
- **Headless CLI Command-Line Executable (`src/cli/main.cpp`)**: Standalone C++ terminal executable (`quebratsk-cli.exe`) for automated build pipelines, CI/CD asset conversion, and headless asset verification.
- **Editor Drag-and-Drop Import Plugin (`demo/addons/quebratsk_editor/import_plugin.gd`)**: `EditorImportPlugin` converting raw `.wad`, `.gma`, `.pbo`, `.vpk`, `.pak`, or `.bundle` files dropped into Godot FileSystem dock into native resource files.
- **VFS Profiler & Memory Telemetry (`src/core/vfs/vfs_telemetry.h/.cpp`)**: Real-time memory-mapped bytes telemetry and C++ microsecond parsing measurement tracker.
- **PBR Auto-Tuner (`src/converters/pbr_autotuner.h/.cpp`)**: Procedural texture processor calculating roughness, metallic, and height map estimation for legacy diffuse maps.
- **Sound Script Parser & Spatial 3D Audio Streamer (`src/parsers/audio/sound_script_parser.h/.cpp`)**: Audio metadata parser and pre-configured `godot::AudioStreamPlayer3D` node generator.
- **Live Asset File Watcher (`src/core/vfs/file_watcher.h/.cpp`)**: Hot-reloading background watcher for detecting disk modifications to mounted archives.
- **Automated LOD Generator (`src/converters/lod_generator.h/.cpp`)**: Quadric Error Metric (QEM) index decimation subsystem for generating LOD1, LOD2, and LOD3 mesh levels.
- **VFS File Tree Explorer Class (`src/api/vfs_file_tree.h/.cpp`)**: C++ class bound to ClassDB returning structured VFS archive trees for the Godot Editor Dock UI.
- **Mod Dependency Resolver (`src/core/vfs/dependency_resolver.h/.cpp`)**: Automatic manifest parser for resolving required parent texture/material archives (`addon.json`, `config.cpp`).
- **Async Multi-Threaded Importer (`src/api/async_asset_importer.h/.cpp`)**: Non-blocking background mesh loading with `load_mesh_async` and `Callable` deferred callbacks.
- **Godot Editor VFS Dock Plugin (`demo/addons/quebratsk_editor/`)**: Editor plugin for browsing mounted VFS archives inside the Godot 4 Editor UI.
- **Beginner's Guide & Tutorial (`TUTORIAL_GODOT4.md`)**: Intuitive, step-by-step GDScript tutorial for installing, mounting archives, importing 3D models, materials, and terrains in Godot 4.
- **Lightmap UV2 & Shader Bridge (`src/converters/lightmap_packer.h/.cpp`, `src/converters/shader_bridge.h/.cpp`)**: Secondary UV2 atlas bin-packing and custom ShaderMaterial generator for water, glass, and $selfillum maps.
- **GLTF Exporter & VFS Audio Decoder (`src/converters/gltf_exporter.h/.cpp`, `src/parsers/audio/audio_decoder.h/.cpp`)**: GLTF 2.0 asset export pipeline and VFS `.wav` audio stream decoder for `AudioStreamWAV`.
- **Texture & Material Memory Cache (`src/core/vfs/texture_cache.h/.cpp`)**: Thread-safe in-memory cache manager preventing redundant texture/material parsing.
- **V-HACD 4.0 Convex Decomposition Subsystem (`src/converters/vhacd_decomposer.h/.cpp`)**: Volumetric approximate convex hull decomposition for 3D physics collision shape generation in Godot 4.
- **Unreal Engine 4/5 Subsystem (Tier 2 Expansion)**: `UEPakParser`, `UAssetParser`.
- **Unity Engine / Escape from Tarkov Subsystem (Tier 2 Expansion)**: `UnityBundleParser`, `UnityMeshParser`.
- **Bohemia Enfusion / Arma Reforger Subsystem (Tier 1 Expansion)**: `EnfusionPakParser`, `XOBParser`.
- **Source Engine 2 / Counter-Strike 2 Subsystem (Tier 1 Expansion)**: `VPK2Parser`, `VMDLParser`.
- **Skeletal Retargeting Engine (Phase 6)**: `SkeletalRetargeter` for Godot 4 `SkeletonProfileHumanoid` bone mapping.
- **Unified GDScript API (Phase 5)**: `UnifiedAssetImporter` class bound to Godot ClassDB.
- **Native Godot 4 Converters (Phase 4)**: `MeshConverter`, `SkeletonConverter`, `MaterialConverter`, `AnimationConverter`, `CollisionConverter`, `TerrainConverter`.
- **Source Engine 1 Parsers (Phase 3b)**: `GMAParser`, `VTFParser`, `VMTParser`, `SourceMDLParser`.
- **Real Virtuality / Enfusion Parsers (Phase 3c)**: `PBOParser`, `PAADecoder`, `P3DMLODParser`, `WRPParser`.
- **GoldSrc Engine Parsers (Phase 3a)**: `WAD3Parser`, `BSP30Parser`, `MDL10Parser`, `SPRParser`.

## [0.1.0-alpha] - 2026-07-29

### Added
- **Core Subsystem Architecture**: Technical design for universal asset interoperability in Godot 4 via GDExtension C++20.
- **Virtual File System (VFS) Specification**: Zero-copy RAM layer with on-the-fly LZSS/LZO decompressors.
