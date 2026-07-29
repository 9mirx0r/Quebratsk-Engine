#pragma once

#include <vector>
#include <godot_cpp/variant/vector3.hpp>

namespace quebratsk::math {

class SIMDMath {
public:
    /// Vectorized Z-Up to Y-Up axis remapping using AVX2 (fallback to scalar if unsupported)
    static void remap_axes_z_up_to_y_up(std::vector<godot::Vector3>& positions);
    
    /// Vectorized Winding Order Inversion (CW to CCW) using AVX2 (fallback to scalar)
    static void invert_winding_order(std::vector<uint32_t>& indices);
    
    /// Check if CPU supports AVX2 at runtime
    static bool is_avx2_supported();
};

} // namespace quebratsk::math
