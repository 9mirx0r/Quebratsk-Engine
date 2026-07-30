#pragma once

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include "../core/vfs/vfs_manager.h"

namespace quebratsk::api {

class VFSDropHandler : public godot::Object {
    GDCLASS(VFSDropHandler, godot::Object)

protected:
    static void _bind_methods();

public:
    VFSDropHandler() = default;
    ~VFSDropHandler() = default;

    /// Automatically mounts dropped files (.wad, .gma, .pbo, .vpk, .pak, .bundle)
    /// into the VFS manager using the filename stem as mount point.
    static bool handle_dropped_files(const godot::PackedStringArray& files, quebratsk::vfs::VFSManager* vfs);
};

} // namespace quebratsk::api
