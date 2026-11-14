#pragma once

#include "../core/ir/ir_mesh_data.h"

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/classes/texture2d.hpp>

#include <cstdint>
#include <vector>

namespace quebratsk::converters {

class TextureLoader;

/// A map's baked light, ready to hang on its surfaces.
///
/// A GoldSrc level is lit by multiplying each wall's texture by a second, much coarser
/// picture of the light that falls on it. Godot's standard material cannot do that the way
/// the game did: it filters every texture it holds the same way, and here the wall texture
/// must stay sharp while the light must be smoothed, or every shadow turns into a staircase
/// of 16-unit squares. So lit surfaces get a small shader of their own instead.
///
/// One of these is made per map and shared by the world and every brush entity in it, so
/// the whole level compiles two shaders rather than two per door.
struct MapLighting {
    godot::Ref<godot::Texture2D> atlas;
    godot::Ref<godot::Shader> opaque;
    godot::Ref<godot::Shader> masked;   // for '{' textures, whose last palette colour is a hole

    /// MAIN THREAD ONLY. Returns an invalid lighting when the map ships no light.
    [[nodiscard]] static MapLighting create(const ir::IRTextureData& lightmap);

    /// MAIN THREAD ONLY. A Source level's light: `rgb` is width * height * 3 linear floats,
    /// top row first, which may run past 1. Invalid when the sizes do not agree.
    [[nodiscard]] static MapLighting create_linear(uint32_t width, uint32_t height,
                                                   const std::vector<float>& rgb);
    [[nodiscard]] bool is_valid() const { return atlas.is_valid(); }
};

/// Names of the shader parameters a lit surface's material carries, for anyone who needs
/// to read the texture back off it.
inline constexpr const char* kLitAlbedoParameter = "albedo_texture";
inline constexpr const char* kLitLightmapParameter = "lightmap_texture";

class MeshConverter {
public:
    /// Convert IRMeshData into a native Godot 4 ArrayMesh.
    ///
    /// When `loader` is non-null, each surface's `material_name` is resolved through the
    /// VFS and attached as a StandardMaterial3D. Passing null yields an untextured mesh.
    ///
    /// When `lighting` is valid, a surface that carries light coordinates (`uv1`) gets a
    /// ShaderMaterial that multiplies its texture by the map's light instead. Its texture
    /// is then the `albedo_texture` shader parameter rather than a material property.
    ///
    /// MAIN THREAD ONLY when `loader` is provided.
    [[nodiscard]] static godot::Ref<godot::ArrayMesh> convert(const ir::IRMeshData& ir_mesh,
                                                              TextureLoader* loader = nullptr,
                                                              const MapLighting* lighting = nullptr);
};

} // namespace quebratsk::converters
