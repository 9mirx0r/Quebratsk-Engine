#include "register_types.h"
#include "core/vfs/vfs_manager.h"
#include "api/unified_asset_importer.h"
#include "api/async_asset_importer.h"
#include "api/vfs_file_tree.h"
#include "api/vfs_drop_handler.h"
#include "api/task_progress_tracker.h"
#include "api/dependency_graph_builder.h"
#include "api/map_preview_viewport.h"
#include "core/config/quebratsk_settings.h"
#include "core/config/import_presets.h"
#include "core/vfs/steam_library_detector.h"
#include "core/vfs/obsidian_doc_exporter.h"
#include "core/vfs/texture_cache.h"
#include "core/logging/quebratsk_logger.h"
#include "converters/fuzzy_material_fixer.h"
#include "converters/winding_visualizer.h"
#include "converters/batching_manager.h"
#include "converters/bsp_map_renderer.h"
#include "converters/vulkan_rt_builder.h"
#include "converters/neural_material_translator.h"
#include "converters/texture_upscaler_pipeline.h"
#include "core/vfs/p2p_vfs_streamer.h"
#include "parsers/unreal/uasset_mesh_extractor.h"
#include "parsers/unity/bundle_mesh_extractor.h"

#include <gdextension_interface.h>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

using namespace godot;

void initialize_quebratsk_module(ModuleInitializationLevel p_level) {
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }

    // Register native C++ classes to Godot ClassDB
    ClassDB::register_class<quebratsk::vfs::VFSManager>();
    ClassDB::register_class<quebratsk::api::UnifiedAssetImporter>();
    ClassDB::register_class<quebratsk::api::AsyncAssetImporter>();
    ClassDB::register_class<quebratsk::api::VFSFileTree>();
    ClassDB::register_class<quebratsk::api::VFSDropHandler>();
    ClassDB::register_class<quebratsk::api::TaskProgressTracker>();
    ClassDB::register_class<quebratsk::api::DependencyGraphBuilder>();
    ClassDB::register_class<quebratsk::api::MapPreviewViewport>();
    ClassDB::register_class<quebratsk::config::QuebratskSettings>();
    ClassDB::register_class<quebratsk::config::ImportPresets>();
    ClassDB::register_class<quebratsk::vfs::SteamLibraryDetector>();
    ClassDB::register_class<quebratsk::vfs::ObsidianDocExporter>();
    ClassDB::register_class<quebratsk::logging::QuebratskLogger>();
    ClassDB::register_class<quebratsk::converters::FuzzyMaterialFixer>();
    ClassDB::register_class<quebratsk::converters::WindingVisualizer>();
    ClassDB::register_class<quebratsk::converters::BatchingManager>();
    ClassDB::register_class<quebratsk::converters::BSPMapRenderer>();
    ClassDB::register_class<quebratsk::converters::VulkanRTBuilder>();
    ClassDB::register_class<quebratsk::converters::NeuralMaterialTranslator>();
    ClassDB::register_class<quebratsk::converters::TextureUpscalerPipeline>();
    ClassDB::register_class<quebratsk::vfs::P2PVFSStreamer>();
    ClassDB::register_class<quebratsk::parsers::unreal::UAssetMeshExtractor>();
    ClassDB::register_class<quebratsk::parsers::unity::BundleMeshExtractor>();

    // Initialize project settings so they appear in the Godot Editor UI
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
