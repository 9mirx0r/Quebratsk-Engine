# Changelog

All notable changes to **Quebratsk Engine** are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [Unreleased]

### Planned
- **Roadmap 1.0 Complete!** All 10 Extreme Performance & QoL features have been implemented.

### Fixed
- **[CRITICAL] BSP30Parser out-of-bounds array indexing**: Added strict bounds checks for `face.first_edge_index`, `num_surfedges`, and `num_edges` during face polygon vertex iteration, preventing out-of-bounds memory reads on corrupted BSP maps.
- **[HIGH] BatchingManager Data Race**: `BatchingManager::register_instance()`, `flush()`, and `clear()` lacked thread synchronization. Added `std::mutex` and `std::lock_guard` to enable safe multi-threaded mesh registration.
- **[PERFORMANCE] MeshConverter Copy-on-Write Overhead**: Replaced scalar `.set(i, val)` calls on Godot's `PackedVector3Array`, `PackedFloat32Array`, and `PackedVector2Array` with direct writable pointer access `.ptrw()` and SIMD-aligned `std::memcpy`.
- **[CRITICAL] OcclusionGenerator thread-safety crash**: `BoxOccluder3D::instantiate()` was called from `std::async` background thread, violating Godot's ClassDB thread-safety. Refactored to return pure `OcclusionResult` struct from background; Godot object creation now happens exclusively on main thread via `create_from_result()`.
- **[CRITICAL] AsyncCollisionBuilder PhysicsServer crash**: `ConcavePolygonShape3D::set_faces()` called `PhysicsServer3D` from background thread. Refactored to `prepare_faces_async()` (pure data copy in background) + `create_shape()` (main thread only).
- **[CRITICAL] VRAMGarbageCollector race condition + Resource destruction crash**: `Ref<Resource>::unref()` could trigger `RenderingServer::free()` from background thread. Replaced with 2-phase eviction: background thread collects candidates, `evict_expired_resources()` frees them on main thread. Also migrated from `std::thread` to `std::jthread` with cooperative `stop_token`, and replaced `OS::get_ticks_msec()` with `std::chrono::steady_clock`.
- **[CRITICAL] LazyMemoryMapper lost base pointer**: `UnmapViewOfFile` was called on an offset pointer via fragile `VirtualQuery` hack. Now stores the original `MapViewOfFile`/`mmap` base pointer in a dedicated `_raw_base_view` field.
- **[HIGH] TextureTranscoder dangling span**: `std::span` (non-owning view) was captured by value in `std::async` lambda, causing use-after-free if caller freed the backing buffer. Now copies bytes into a `shared_ptr<vector<byte>>` before dispatching.
- **[HIGH] QuebratskSettings never registered**: `GDCLASS` was declared but `ClassDB::register_class` and `register_settings()` were never called. Added both to `register_types.cpp`.
- **[HIGH] LazyMemoryMapper 64-bit truncation**: File offsets were stored as `DWORD` (32-bit), silently truncating offsets for files >4GB. Changed to `uint64_t` arithmetic.
- **[HIGH] VRAMGarbageCollector used std::thread**: Replaced with `std::jthread` + `std::stop_token` for cooperative cancellation and automatic join-on-destruct, preventing 5-second hangs during editor shutdown.

### Added
- **In-Editor "Map Fly-Through" Previewer (`src/api/map_preview_viewport.h/.cpp`)**: `MapPreviewViewport` sub-viewport class allowing live 3D map fly-through previews directly inside the Godot editor without launching game execution.
- **Clickable Formatted Console Logger (`src/core/logging/quebratsk_logger.h/.cpp`)**: `QuebratskLogger` subsystem wrapping `UtilityFunctions::push_error` and `push_warning` to print clean, formatted messages with clickable VFS asset targets.
- **Interactive Winding Order Visualizer (`src/converters/winding_visualizer.h/.cpp`)**: `WindingVisualizer` tool injecting debug materials to highlight back-faces in bright red and providing one-click normal flipping.
- **Obsidian Auto-Doc Exporter (`src/core/vfs/obsidian_doc_exporter.h/.cpp`)**: `ObsidianDocExporter` class formatting current VFS mount states and scene node trees into Markdown documentation for sync with Obsidian Vaults.
- **Native Background Task Progress Tracker (`src/api/task_progress_tracker.h/.cpp`)**: `TaskProgressTracker` class bound to ClassDB providing thread-safe atomic percentage tracking and status string reporting for Godot Editor progress bar overlays.
- **Fuzzy-Match Material Auto-Fixer (`src/converters/fuzzy_material_fixer.h/.cpp`)**: `FuzzyMaterialFixer` subsystem calculating Levenshtein distance string matching across VFS file lists to automatically recover missing texture files.
- **Asset Dependency Graph Builder (`src/api/dependency_graph_builder.h/.cpp`)**: `DependencyGraphBuilder` class generating structured Dictionary node trees representing asset dependencies for rendering in Godot's GraphEdit UI.
- **Drag-and-Drop VFS Mounting (`src/api/vfs_drop_handler.h/.cpp`)**: `VFSDropHandler` class bound to ClassDB that automatically mounts dropped `.wad`, `.gma`, `.pbo`, `.vpk`, `.pak`, or `.bundle` files using the filename stem as the mount point.
- **One-Click Import Presets (`src/core/config/import_presets.h/.cpp`)**: `ImportPresets` helper providing 3 simple presets (`MAX_PERFORMANCE`, `RETRO_FIDELITY`, `MAX_QUALITY`) to configure VRAM eviction timeouts, thread limits, and shader pre-caching with a single call.
- **Steam Library Auto-Detection (`src/core/vfs/steam_library_detector.h/.cpp`)**: `SteamLibraryDetector` scanning Win32 registry `HKCU\Software\Valve\Steam` and `libraryfolders.vdf` to discover installed games (Half-Life, CS 1.6, Garry's Mod, CS2, Arma 3, DayZ) for instant mounting.
- **Smart VRAM Garbage Collector (`vram_garbage_collector.h/.cpp`)**: Background `jthread` tracking resource access timestamps via `OS::get_ticks_msec()` and automatically calling `.unref()` on cached textures/meshes that exceed the idle timeout to prevent OOM crashes on low-end hardware.
- **Automated Shader Pre-Caching (`shader_precacher.h/.cpp`)**: Compiles all materials into dummy Pipeline State Objects (PSOs) via Godot's `RenderingServer` during map load to eliminate runtime shader compilation stutter.
- **Multi-Threaded Collision BVH Builder (`async_collision_builder.h/.cpp`)**: Offloads `ConcavePolygonShape3D` generation for massive map chunks to `std::async`, preserving main thread framerate.
- **Configurable Memory Quotas via UI (`quebratsk_settings.h/.cpp`)**: Registers new sliders in `ProjectSettings` (e.g., `quebratsk/performance/vram_eviction_timeout_msec`) to give developers full control over the engine's extreme limits directly from the Godot Editor.
- **Asynchronous Occlusion Culling Hull Generator (`src/core/vfs/occlusion_generator.h/.cpp`)**: Background `jthread` extracting AABB/OBB bounding volumes from raw meshes to generate Godot `BoxOccluder3D` nodes, preventing GPU overdraw.
- **Lazy-Loaded VFS Streaming (`src/core/vfs/lazy_memory_mapper.h/.cpp`)**: True zero-memory mapping using `MapViewOfFile` to map only requested byte windows of large archives into RAM, dropping base memory footprint to under 100MB.
- **Instanced Rendering Auto-Batching (`src/converters/batching_manager.h/.cpp`)**: Consolidates duplicate `MeshInstance3D` spawn requests into a single `MultiMeshInstance3D` automatically to minimize CPU draw calls.
- **Background Texture Transcoding (`src/core/vfs/texture_transcoder.h/.cpp`)**: `std::jthread` pool architecture for real-time legacy PC texture (DXT1/5) decoding into Godot mobile/web compatible RGBA8/ETC2 buffers.
- **SIMD-Accelerated Math (`src/core/math/simd_math.h/.cpp`)**: AVX2 and ARM Neon vectorization for vertex array Z-Up to Y-Up remapping and winding order inversion.
- **Zero-Copy Vulkan/DirectX Staging Buffers (`src/core/vfs/gpu_direct_buffer.h/.cpp`)**: Direct RAM to VRAM staging buffer creation using `PackedByteArray` aliasing.
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
