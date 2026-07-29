#include "register_types.h"
#include "core/vfs/vfs_manager.h"
#include "api/unified_asset_importer.h"
#include "api/async_asset_importer.h"
#include "api/vfs_file_tree.h"

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
}

void uninitialize_quebratsk_module(ModuleInitializationLevel p_level) {
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }
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
