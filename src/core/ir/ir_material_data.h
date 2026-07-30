#pragma once

#include <optional>
#include <string>

#include "ir_mesh_data.h"
#include <godot_cpp/variant/color.hpp>

namespace quebratsk::ir {

struct IRTextureRef {
    std::string vfs_path;
    int32_t width = 0;
    int32_t height = 0;
};

struct IRMaterialData {
    SourceEngine source_engine = SourceEngine::GoldSrc;
    std::string name;
    std::string shader_name; // Source shader: "VertexLitGeneric", "NormalMapSpecularDIMap"

    std::optional<IRTextureRef> albedo_texture;
    std::optional<IRTextureRef> normal_texture;
    std::optional<IRTextureRef> specular_texture;
    std::optional<IRTextureRef> emission_texture;
    std::optional<IRTextureRef> ao_texture;
    std::optional<IRTextureRef> detail_texture;

    godot::Color albedo_color = godot::Color(1, 1, 1, 1);
    float metallic = 0.0f;
    float roughness = 0.5f;
    float normal_scale = 1.0f;
    float alpha_cutoff = 0.5f;

    bool is_transparent = false;
    bool is_additive = false;
    bool is_two_sided = false;
    bool has_compressed_normal = false; // DXT5nm de-swizzle flag
};

} // namespace quebratsk::ir
