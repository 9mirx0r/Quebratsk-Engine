#include "shader_bridge.h"

namespace quebratsk::converters {

using namespace godot;

Ref<ShaderMaterial> ShaderBridge::create_specialized_material(const ir::IRMaterialData& ir_material) {
    Ref<ShaderMaterial> shader_mat;
    shader_mat.instantiate();
    return shader_mat;
}

} // namespace quebratsk::converters
