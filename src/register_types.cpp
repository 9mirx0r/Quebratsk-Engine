#include "register_types.h"
#include "core/vfs/vfs_manager.h"
#include "api/unified_asset_importer.h"
#include "api/async_asset_importer.h"
#include "api/vfs_drop_handler.h"
#include "api/task_progress_tracker.h"
#include "api/map_preview_viewport.h"
#include "core/config/quebratsk_settings.h"
#include "core/vfs/steam_library_detector.h"
#include "core/vfs/obsidian_doc_exporter.h"
#include "core/vfs/texture_cache.h"
#include "core/logging/quebratsk_logger.h"
#include "converters/fuzzy_material_fixer.h"
#include "converters/winding_visualizer.h"
#include "converters/batching_manager.h"
#include "converters/material_heuristics.h"
#include "converters/texture_upscaler_pipeline.h"

#include <gdextension_interface.h>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

using namespace godot;

void initialize_quebratsk_module(ModuleInitializationLevel p_level) {
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }

    // Every class registered here does what its name says. Seven were removed for 2.0
    // because they did not:
    //
    //   UAssetMeshExtractor, BundleMeshExtractor  returned a named mesh with zero
    //                                             vertices, so Unreal and Unity "support"
    //                                             was a surface with a material name
    //   VulkanRTBuilder                           reported is_built: true for an
    //                                             acceleration structure Godot 4.3 has no
    //                                             API to build
    //   P2PVFSStreamer                            contained no networking at all
    //   BSPMapRenderer                            not implemented; use
    //                                             UnifiedAssetImporter.load_mesh()
    //   VFSFileTree                               returned one hardcoded row
    //   DependencyGraphBuilder                    returned an always-empty dependency list
    //
    // A registered class is a promise. Deleting them is the honest version of a 2.0.
    ClassDB::register_class<quebratsk::vfs::VFSManager>();
    ClassDB::register_class<quebratsk::api::UnifiedAssetImporter>();
    ClassDB::register_class<quebratsk::api::AsyncAssetImporter>();
    ClassDB::register_class<quebratsk::api::VFSDropHandler>();
    ClassDB::register_class<quebratsk::api::TaskProgressTracker>();
    ClassDB::register_class<quebratsk::api::MapPreviewViewport>();
    ClassDB::register_class<quebratsk::config::QuebratskSettings>();
    ClassDB::register_class<quebratsk::vfs::SteamLibraryDetector>();
    ClassDB::register_class<quebratsk::vfs::ObsidianDocExporter>();
    ClassDB::register_class<quebratsk::logging::QuebratskLogger>();
    ClassDB::register_class<quebratsk::converters::FuzzyMaterialFixer>();
    ClassDB::register_class<quebratsk::converters::WindingVisualizer>();
    ClassDB::register_class<quebratsk::converters::BatchingManager>();
    ClassDB::register_class<quebratsk::converters::MaterialHeuristics>();
    ClassDB::register_class<quebratsk::converters::TextureUpscalerPipeline>();

    quebratsk::config::QuebratskSettings::register_settings();
}

void uninitialize_quebratsk_module(ModuleInitializationLevel p_level) {
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }

    // Function-local statics inside a shared library are destroyed at library unload,
    // which happens *after* Godot has torn down ClassDB and the RenderingServer. Any
    // Ref<Resource> still held at that point releases its last reference into a dead
    // engine and crashes on exit. Drop every Godot handle here, while the servers are
    // still alive; the static destructors then have nothing left to free.
    quebratsk::vfs::TextureCache::instance().clear();
}

extern "C" {
GDExtensionBool GDE_EXPORT quebratsk_library_init(
    GDExtensionInterfaceGetProcAddress p_get_proc_address,
    const GDExtensionClassLibraryPtr p_library,
    GDExtensionInitialization *r_initialization) {

    godot::GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);
    init_obj.register_initializer(initialize_quebratsk_module);
    init_obj.register_terminator(uninitialize_quebratsk_module);
    init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);

    return init_obj.init();
}
}
