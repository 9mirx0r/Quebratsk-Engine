#pragma once

#include "../core/ir/ir_skeleton_data.h"

#include <godot_cpp/classes/skeleton3d.hpp>

namespace quebratsk::converters {

class SkeletonConverter {
public:
    /// Convert Intermediate Representation IRSkeletonData into native Godot 4 Skeleton3D node
    [[nodiscard]] static godot::Skeleton3D* convert(const ir::IRSkeletonData& ir_skeleton);
};

} // namespace quebratsk::converters
