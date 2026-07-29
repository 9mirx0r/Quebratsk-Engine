#pragma once

#include "../core/ir/ir_material_data.h"

namespace quebratsk::converters {

class PBRAutoTuner {
public:
    /// Compute procedural PBR roughness, metallic, and normal height maps for legacy diffuse textures
    static void autotune_material(ir::IRMaterialData& ir_material);
};

} // namespace quebratsk::converters
