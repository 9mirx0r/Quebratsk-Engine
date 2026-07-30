#pragma once

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include "../core/vfs/vfs_manager.h"
#include <string>
#include <vector>

namespace quebratsk::converters {

class FuzzyMaterialFixer : public godot::Object {
    GDCLASS(FuzzyMaterialFixer, godot::Object)

protected:
    static void _bind_methods();

public:
    FuzzyMaterialFixer() = default;
    ~FuzzyMaterialFixer() = default;

    /// Finds the closest matching texture path inside the VFS using Levenshtein distance
    static godot::String find_best_matching_texture(
        const godot::String& missing_texture_name,
        quebratsk::vfs::VFSManager* vfs
    );

private:
    static size_t _levenshtein_distance(const std::string& s1, const std::string& s2);
};

} // namespace quebratsk::converters
