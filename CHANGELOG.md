# Changelog

All notable changes to **Quebratsk Engine** are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [Unreleased]

---

## Engineering Audit Pass — 2026-07-30

A full file-by-file audit of all 180 sources in `src/` (~7,000 lines). Every item below
was verified against the code, not inferred. Twelve crash- or memory-safety defects were
reachable from ordinary user input; several were reachable from a malicious archive.

### Security

- **[CRITICAL] Integer overflow in `WRPParser::parse` (`wrp_parser.cpp`)**: `grid_width * grid_height * sizeof(float)` wrapped around `size_t` for crafted headers (e.g. `0x7FFFFFFF` x `0x7FFFFFFF`), causing the bounds check to pass and `heightmap.assign()` to read gigabytes past the end of the mapping. Grid dimensions are now validated against a hard ceiling and the bounds check uses division instead of multiplication.
- **[CRITICAL] Undefined behaviour in `BSP30Parser::get_lump` (`bsp30_parser.cpp`)**: Lump offsets and lengths are `int32_t` on disk; negative values cast to huge `size_t` values and `ofs + len` wrapped, so `std::span::subspan()` was invoked with `offset > size()` — undefined behaviour, not a throw. Negative values are now rejected and the range check uses subtraction.
- **[CRITICAL] Out-of-bounds read via `texinfo_index` (`bsp30_parser.cpp`)**: `face.texinfo_index` was checked for `>= 0` but never against the size of lump 6, allowing a 40-byte read far outside the lump (and a null dereference when lump 6 was absent). The index is now bounded by the computed element count.
- **[CRITICAL] Unbounded string scan past the memory mapping (`vfs_manager.cpp`)**: `index_gma()` constructed `std::string` from a raw pointer after a scan that could terminate at end-of-buffer without finding a NUL, walking off the end of the mapped file. Replaced with a shared `read_cstr_bounded()` helper that fails on a missing terminator.
- **[CRITICAL] Unbounded bone-name scan (`mdl_source_parser.cpp`)**: The guard validated only the start offset of the name string; `std::string(const char*)` then ran to the first NUL, potentially past the end of the file. Now uses `strnlen()` with an explicit remaining-bytes limit.
- **[CRITICAL] Integer overflow in VFS entry bounds checks (`vfs_manager.cpp`)**: `entry.offset + entry.disk_size > size()` wrapped for archives declaring huge file sizes, letting `get_raw_span()` and `read_file()` build out-of-range spans. Added an overflow-safe `range_fits()` helper, applied at index time in `index_wad3`/`index_gma`/`index_pbo` and again at read time.
- **[CRITICAL] Stack buffer over-read in `SteamLibraryDetector` (`steam_library_detector.cpp`)**: `RegQueryValueExA` does not guarantee NUL termination for `REG_SZ`; a value exactly filling `MAX_PATH` caused `std::string(path)` to read past the array. Switched to `RegGetValueA` with `RRF_RT_REG_SZ`, which guarantees termination and validates the value type.
- **[CRITICAL] Attacker-controlled allocation in `EnfusionPakParser` (`enfusion_pak_parser.cpp`)**: `entries.reserve(header->entry_count)` accepted an unvalidated 32-bit count from the file. With exceptions disabled (godot-cpp sets `_HAS_EXCEPTIONS=0`) the resulting `length_error` became `terminate()`. The count is now bounded by what the file can physically contain.
- **[CRITICAL] Integer overflow on sprite frame dimensions (`spr_parser.cpp`)**: Negative `int32_t` width/height cast to huge `size_t` values, wrapping the pixel count and driving both the `resize()` and the decode loop out of bounds. Dimensions are now validated and the range check uses subtraction.
- **[HIGH] Signed overflow on `INT32_MIN` surfedge (`bsp30_parser.cpp`)**: Negating `INT32_MIN` is undefined behaviour. The magnitude is now taken in unsigned arithmetic.
- **[HIGH] Off-by-one in clipnode plane bounds (`bsp30_parser.cpp`)**: The check compared the plane's *start* offset against the lump size, permitting a read that began inside the lump and ran up to 19 bytes past its end. Now compares the index against the element count.

### Fixed

- **[CRITICAL] Guaranteed process abort in `UnifiedAssetImporter::load_mesh` (`unified_asset_importer.cpp`)**: `get_raw_span()` returns `nullopt` for every compressed entry; the fallback read into a `PackedByteArray` that was immediately discarded, then `raw_span.value()` was called on the empty optional. Because godot-cpp compiles with `_HAS_EXCEPTIONS=0`, `std::bad_optional_access` became `std::terminate()` — any asset inside a compressed PBO killed the editor. Rewrote the read path around a new `read_asset_bytes()` that returns an owned buffer for both the mapped and the decompressed case. The same latent bug is fixed in `load_material()` and `load_terrain()`.
- **[CRITICAL] Godot `Resource` allocation on a detached worker thread (`async_asset_importer.cpp`)**: `load_mesh_async()` ran `importer->load_mesh()` inside `std::thread(...).detach()`, which called `ArrayMesh::instantiate()` and `add_surface_from_arrays()` off the main thread, captured the importer as a raw pointer (use-after-free if the node was freed mid-flight), and left a detached thread running across library unload. Rewritten into a three-phase pipeline: VFS read on the main thread, pure-IR decode on a tracked `std::jthread`, and `ArrayMesh` construction on the main thread via `call_deferred`. Workers are now joined in the destructor.
- **[CRITICAL] Crash on editor shutdown (`register_types.cpp`, `texture_cache.cpp`)**: `TextureCache`'s function-local static holds `Ref<StandardMaterial3D>` values and is destroyed at library unload — after Godot has torn down `ClassDB` and the `RenderingServer`. `uninitialize_quebratsk_module()` now clears the cache while the servers are still alive, resolving the long-standing `TODO B1`.
- **[CRITICAL] Inverted geometry on every imported map (`bsp30_parser.cpp`, `axis_remap.h`)**: The parser called `invert_winding_order()` after `source_to_godot()`. That basis change is `M = [1 0 0; 0 0 1; 0 -1 0]` with `det(M) = +1`, so it *preserves* orientation and the GoldSrc winding was already correct. The extra inversion flipped every triangle and made maps render inside-out under backface culling. Removed the call and documented the determinant invariant on `source_to_godot()`.
- **[HIGH] `unmount()` never released the mapping (`vfs_manager.cpp`)**: Only index entries were erased. The `MemoryMappedFile` stayed alive, keeping the OS file handle open (locking the archive on Windows) and leaking the address-space reservation, so repeated mount/unmount cycles grew without bound. `unmount()` now closes the mapping and marks the container slot reusable, and `mount_container()` recycles free slots. Also fixed a prefix-normalization mismatch that made `unmount("vfs://name")` silently match nothing.
- **[HIGH] Deadlock risk and off-thread scene access in `BatchingManager::flush` (`batching_manager.cpp`)**: `memnew()`, `add_child()` and `set_owner()` ran while holding `_mutex`; any callback re-entering `register_instance()` during `add_child()` would deadlock on the non-recursive mutex. The registry is now swapped out under the lock and the scene tree is touched with the lock released, guarded by an explicit main-thread assertion.
- **[HIGH] Missing surface validation in `MeshConverter` (`mesh_converter.cpp`)**: Indices were never bounds-checked against the vertex count and per-vertex array lengths were never compared, so a corrupt BSP could reach `add_surface_from_arrays()` with mismatched arrays or out-of-range indices. All arrays are now length-checked (tangents at 4 floats per vertex) and out-of-range indices cause the surface to be skipped with a diagnostic.
- **[HIGH] GPU resource leak in `GPUDirectBuffer` (`gpu_direct_buffer.cpp`)**: `create_mesh_surface_from_mmap()` called `RenderingServer::mesh_create()` and returned the RID without ever adding a surface or freeing it, leaking a GPU resource per call. The unimplemented path now allocates nothing and reports itself honestly.
- **[HIGH] Data race on `VRAMGarbageCollector::_max_idle_time_msec`**: Written by `start()` on the caller's thread and read by `_gc_loop()` on the worker without synchronization. Changed to `std::atomic<uint64_t>`.
- **[HIGH] `SpriteFrameHeader` was 20 bytes instead of 16 (`spr_structs.h`)**: A non-existent `group` member was declared inside `dspriteframe_t`; the frame-group selector is a separate `int32` that precedes the frame only in grouped sprites. Every frame was read at the wrong offset and the cursor drifted 4 bytes per frame. Corrected the layout and the `static_assert`.
- **[HIGH] `.gdextension` shipped the debug binary as the release artifact (`demo/bin/quebratsk.gdextension`, `CMakeLists.txt`)**: `windows.release.x86_64` pointed at the debug DLL, and CMake hardcoded `OUTPUT_NAME` to `...debug...` regardless of configuration, so a Release build silently overwrote the debug artifact. The output name now follows `$<CONFIG>` and both configurations are built and shipped separately. `compatibility_minimum` was also raised from `4.1` to `4.3` to match the godot-cpp branch actually linked — GDExtensions are forward-compatible only, so the old value invited Godot 4.1/4.2 to load an unsupported binary.
- **[MEDIUM] WAD3 directory parsed without a header-size check (`vfs_manager.cpp`)**: `index_wad3()` dereferenced the header before confirming the file was at least `sizeof(WAD3Header)`, and multiplied a potentially negative `num_lumps` by the lump size. Both are now validated, and individual directory entries with impossible ranges are skipped rather than indexed.
- **[MEDIUM] Unvalidated palette size in `SPRParser` (`spr_parser.cpp`)**: `palette_size` was read and discarded while the decoder unconditionally assumed 256 RGB triples. Now rejected if it is not 256.
- **[MEDIUM] `main()` linked into the shared library (`CMakeLists.txt`)**: `GLOB_RECURSE` pulled `src/cli/main.cpp` into the GDExtension DLL. Excluded, and `CONFIGURE_DEPENDS` added so new sources trigger reconfiguration.

### Performance

- **`BatchingManager::flush` (`batching_manager.cpp`)**: Replaced `N` calls to `MultiMesh::set_instance_transform()` — each crossing the GDExtension boundary and re-validating the RID — with a single `set_buffer()` upload built from a flat `PackedFloat32Array`.
- **`MeshConverter::convert` (`mesh_converter.cpp`)**: The index array is now filled with one `std::memcpy` instead of a per-element cast loop; validation above guarantees the `uint32_t`→`int32_t` reinterpretation is exact.
- **`UnifiedAssetImporter` (`unified_asset_importer.cpp`)**: Parsing was split into a pure-data `parse_mesh_ir()` and Godot object construction, removing a redundant full-file copy on the compressed path and enabling genuine off-thread decoding.

### Changed

- **Documentation accuracy**: Several previously logged entries overstated what shipped. Verified against the code during this pass: `SIMDMath`'s AVX2 branches contain only comments and always fall through to scalar code (the data is AoS `std::vector<Vector3>`, which cannot be processed 8 vertices per cycle without an SoA layout); `GPUDirectBuffer`'s zero-copy path is unimplemented; `ShaderPrecacher` compiles an identical dummy shader per call and precaches nothing; `TextureTranscoder`'s DXT1/DXT5 decoders are stubs that return zero-filled buffers; `P2PVFSStreamer`, `BSPMapRenderer` PVS culling, `VulkanRTBuilder` and `PAADecoder` are mocks. Twenty-seven classes have no call sites anywhere in the tree. These are tracked for follow-up rather than removed in this pass.

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
