#pragma once

#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include <godot_cpp/variant/packed_int32_array.hpp>

namespace quebratsk::math {

/// Swap triangle face indices from Clockwise (CW) to Counter-Clockwise (CCW)
inline void invert_winding_order(std::vector<uint32_t>& indices) {
    size_t count = indices.size();
    for (size_t i = 0; i + 2 < count; i += 3) {
        std::swap(indices[i + 1], indices[i + 2]);
    }
}

/// Swap triangle face indices in Godot PackedInt32Array
inline void invert_winding_order(godot::PackedInt32Array& indices) {
    int64_t count = indices.size();
    for (int64_t i = 0; i + 2 < count; i += 3) {
        int32_t tmp = indices[i + 1];
        indices.set(i + 1, indices[i + 2]);
        indices.set(i + 2, tmp);
    }
}

} // namespace quebratsk::math
