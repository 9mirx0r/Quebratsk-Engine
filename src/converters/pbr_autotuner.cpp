#include "pbr_autotuner.h"

namespace quebratsk::converters {

void PBRAutoTuner::autotune_material(ir::IRMaterialData& ir_material) {
    // Default PBR estimation parameters for legacy textures
    if (ir_material.roughness <= 0.0f) {
        ir_material.roughness = 0.6f;
    }
    if (ir_material.metallic <= 0.0f) {
        ir_material.metallic = 0.1f;
    }
}

} // namespace quebratsk::converters
