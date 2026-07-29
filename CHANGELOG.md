# Changelog

All notable changes to **Quebratsk Engine** are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [Unreleased]

### Planned (10 QoL Superpowers Expansion Roadmap)
- Interactive VFS File Tree Explorer with 3D Thumbnail Previews in Editor Dock (`addons/quebratsk_editor/`).
- Automatic Mod Dependency Resolver & Conflict Manager (`addon.json` & `config.cpp` manifest parsers).
- Hot-Reloading Live Asset File Watcher in `VFSManager`.
- Automated LOD (Level of Detail) Mesh Simplification Subsystem (Quadric Error Metric).
- Legacy-to-PBR Auto-Tuning Shader Filter (Procedural normal, roughness, and metallic generation).
- Spatial 3D Audio Bank Streamer & Attenuator (`AudioStreamPlayer3D` script parser).
- One-Click Drag-and-Drop Editor Importer Plugin (`EditorImportPlugin`).
- Real-Time Telemetry & Performance Profiler Overlay.
- Universal Batch GLTF / OBJ Asset Converter GUI utility.
- Headless CLI Command-Line Tool (`quebratsk-cli.exe`) for automated CI/CD pipelines.

### Added
- **Async Multi-Threaded Importer (`src/api/async_asset_importer.h/.cpp`)**: Non-blocking background mesh loading with `load_mesh_async` and `Callable` deferred callbacks.
- **Godot Editor VFS Dock Plugin (`demo/addons/quebratsk_editor/`)**: Editor plugin for browsing mounted VFS archives inside the Godot 4 Editor UI.
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
