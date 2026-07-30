#include "mdl10_parser.h"
#include "../../core/math/axis_remap.h"

#include <godot_cpp/variant/basis.hpp>
#include <godot_cpp/variant/transform3d.hpp>

#include <cstring>
#include <unordered_map>
#include <vector>

namespace quebratsk::parsers::goldsrc {

namespace {

/// Overflow-safe "do `count` elements of `elem_size` fit at `offset`?".
bool fits(size_t offset, size_t count, size_t elem_size, size_t total) {
    if (offset > total) return false;
    if (elem_size == 0) return count == 0;
    return count <= (total - offset) / elem_size;
}

/// StudioMDL stores XYZ Euler angles applied X, then Y, then Z, matching Valve's
/// AngleQuaternion(). Godot's Basis/Quaternion Euler constructors use YXZ, so the
/// composition has to be written out explicitly.
godot::Basis studio_euler_to_basis(float rx, float ry, float rz) {
    const godot::Quaternion qx(godot::Vector3(1, 0, 0), rx);
    const godot::Quaternion qy(godot::Vector3(0, 1, 0), ry);
    const godot::Quaternion qz(godot::Vector3(0, 0, 1), rz);
    return godot::Basis(qz * qy * qx);
}

/// Rest transform of every bone in MODEL space, still in the source Z-up frame.
///
/// This is not optional bookkeeping: GoldSrc stores each vertex in the local space of
/// the bone it is attached to, so without walking the hierarchy the mesh comes out
/// scattered around the origin.
std::vector<godot::Transform3D> build_bone_rest_transforms(const StudioBone* bones, int32_t count) {
    std::vector<godot::Transform3D> world(static_cast<size_t>(count));

    for (int32_t i = 0; i < count; ++i) {
        const auto& b = bones[i];
        const godot::Transform3D local(
            studio_euler_to_basis(b.value[3], b.value[4], b.value[5]),
            godot::Vector3(b.value[0], b.value[1], b.value[2]));

        // StudioMDL orders bones parents-first; the bounds test also guards a corrupt
        // or cyclic hierarchy, which would otherwise read an uninitialised transform.
        if (b.parent >= 0 && b.parent < i) {
            world[static_cast<size_t>(i)] = world[static_cast<size_t>(b.parent)] * local;
        } else {
            world[static_cast<size_t>(i)] = local;
        }
    }
    return world;
}

/// One corner emitted by the triangle command stream.
struct TriCorner {
    int16_t vertex = 0;
    int16_t normal = 0;
    int16_t s = 0;
    int16_t t = 0;
};

uint64_t corner_key(const TriCorner& c) {
    return (static_cast<uint64_t>(static_cast<uint16_t>(c.vertex)) << 48) |
           (static_cast<uint64_t>(static_cast<uint16_t>(c.normal)) << 32) |
           (static_cast<uint64_t>(static_cast<uint16_t>(c.s)) << 16) |
           (static_cast<uint64_t>(static_cast<uint16_t>(c.t)));
}

/// Decode the 8-bit palettized pixels embedded in the model into RGBA8.
/// Layout at `tex.index`: width*height indices, followed by a 256-entry RGB palette.
bool decode_embedded_texture(std::span<const std::byte> mdl, const StudioTexture& tex,
                             ir::IRTextureData& out) {
    if (tex.index <= 0 || tex.width <= 0 || tex.height <= 0 ||
        tex.width > 4096 || tex.height > 4096) {
        return false;
    }

    const size_t offset = static_cast<size_t>(tex.index);
    const size_t pixels = static_cast<size_t>(tex.width) * static_cast<size_t>(tex.height);
    constexpr size_t kPaletteBytes = 256 * 3;

    if (!fits(offset, pixels + kPaletteBytes, 1, mdl.size())) return false;

    const auto* indices = reinterpret_cast<const uint8_t*>(mdl.data() + offset);
    const auto* palette = indices + pixels;

    out.name.assign(tex.name, strnlen(tex.name, sizeof(tex.name)));
    out.width = static_cast<uint32_t>(tex.width);
    out.height = static_cast<uint32_t>(tex.height);

    // Flag 0x40 (STUDIO_NF_MASKED) marks palette index 255 as transparent, the same
    // convention WAD3 uses for '{'-prefixed textures.
    const bool masked = (tex.flags & 0x40) != 0;
    out.has_alpha = masked;

    out.rgba8_pixels.resize(pixels * 4);
    for (size_t i = 0; i < pixels; ++i) {
        const uint8_t idx = indices[i];
        out.rgba8_pixels[i * 4 + 0] = palette[idx * 3 + 0];
        out.rgba8_pixels[i * 4 + 1] = palette[idx * 3 + 1];
        out.rgba8_pixels[i * 4 + 2] = palette[idx * 3 + 2];
        out.rgba8_pixels[i * 4 + 3] = (masked && idx == 255) ? 0 : 255;
    }
    return true;
}

} // namespace

std::expected<ParsedMDL10Model, MDL10ParseError> MDL10Parser::parse(
    std::span<const std::byte> mdl_bytes,
    std::span<const std::byte> texture_mdl_bytes
) {
    if (mdl_bytes.size() < sizeof(StudioHeader)) {
        return std::unexpected(MDL10ParseError::InvalidHeader);
    }

    auto* header = reinterpret_cast<const StudioHeader*>(mdl_bytes.data());
    if (std::memcmp(header->magic, kMdl10Magic.data(), 4) != 0 || header->version != kMdl10Version) {
        return std::unexpected(MDL10ParseError::VersionMismatch);
    }

    ParsedMDL10Model result;
    result.mesh_data.source_engine = ir::SourceEngine::GoldSrc;
    result.mesh_data.name = std::string(header->name, strnlen(header->name, sizeof(header->name)));

    result.skeleton_data.source_engine = ir::SourceEngine::GoldSrc;
    result.skeleton_data.name = result.mesh_data.name;

    // ---------------------------------------------------------------- Bones ----
    const StudioBone* bones = nullptr;
    std::vector<godot::Transform3D> bone_rest; // model space, source Z-up frame

    if (header->num_bones > 0 && header->bone_index > 0 &&
        fits(static_cast<size_t>(header->bone_index),
             static_cast<size_t>(header->num_bones), sizeof(StudioBone), mdl_bytes.size())) {

        bones = reinterpret_cast<const StudioBone*>(mdl_bytes.data() + header->bone_index);
        bone_rest = build_bone_rest_transforms(bones, header->num_bones);

        for (int32_t i = 0; i < header->num_bones; ++i) {
            const auto& b = bones[i];
            ir::IRBone ir_b;
            ir_b.name = std::string(b.name, strnlen(b.name, sizeof(b.name)));
            ir_b.parent_index = b.parent;
            ir_b.position = math::source_to_godot(godot::Vector3(b.value[0], b.value[1], b.value[2]));
            ir_b.rotation = math::source_quat_to_godot(
                godot::Quaternion(studio_euler_to_basis(b.value[3], b.value[4], b.value[5])));
            result.skeleton_data.bones.push_back(std::move(ir_b));
        }
    }

    // ------------------------------------------------------------- Textures ----
    // Embedded textures let the model import fully textured with no VFS lookup.
    // When num_textures is 0 they live in a companion "<name>T.mdl" instead, which
    // this parser does not load — surfaces then carry only a material name.
    const StudioTexture* textures = nullptr;
    size_t num_textures = 0;

    // Textures live either in this file or in the companion "<name>T.mdl", which has
    // the same header layout with its own texture table. Pick whichever declares them.
    std::span<const std::byte> tex_source = mdl_bytes;
    const StudioHeader* tex_header = header;

    if (header->num_textures <= 0 && !texture_mdl_bytes.empty() &&
        texture_mdl_bytes.size() >= sizeof(StudioHeader)) {
        const auto* t_hdr = reinterpret_cast<const StudioHeader*>(texture_mdl_bytes.data());
        if (std::memcmp(t_hdr->magic, kMdl10Magic.data(), 4) == 0 && t_hdr->num_textures > 0) {
            tex_source = texture_mdl_bytes;
            tex_header = t_hdr;
        }
    }

    if (tex_header->num_textures > 0 && tex_header->texture_index > 0 &&
        fits(static_cast<size_t>(tex_header->texture_index),
             static_cast<size_t>(tex_header->num_textures), sizeof(StudioTexture), tex_source.size())) {

        textures = reinterpret_cast<const StudioTexture*>(tex_source.data() + tex_header->texture_index);
        num_textures = static_cast<size_t>(tex_header->num_textures);

        result.mesh_data.embedded_textures.resize(num_textures);
        for (size_t i = 0; i < num_textures; ++i) {
            ir::IRTextureData decoded;
            if (decode_embedded_texture(tex_source, textures[i], decoded)) {
                result.mesh_data.embedded_textures[i] = std::move(decoded);
            }
        }
    }

    // The skin table always comes from the model file, never the texture companion.

    // Skin table: int16[num_skin_families][num_skin_ref]. A mesh's skin_ref indexes a
    // row of family 0 to reach the real texture.
    const int16_t* skin_table = nullptr;
    size_t num_skin_ref = 0;

    if (header->num_skin_ref > 0 && header->skin_index > 0 &&
        fits(static_cast<size_t>(header->skin_index),
             static_cast<size_t>(header->num_skin_ref), sizeof(int16_t), mdl_bytes.size())) {
        skin_table = reinterpret_cast<const int16_t*>(mdl_bytes.data() + header->skin_index);
        num_skin_ref = static_cast<size_t>(header->num_skin_ref);
    }

    auto resolve_texture = [&](int32_t skin_ref) -> int32_t {
        if (skin_ref < 0) return -1;
        size_t tex_idx = static_cast<size_t>(skin_ref);
        if (skin_table && tex_idx < num_skin_ref) {
            const int16_t mapped = skin_table[tex_idx];
            if (mapped < 0) return -1;
            tex_idx = static_cast<size_t>(mapped);
        }
        return tex_idx < num_textures ? static_cast<int32_t>(tex_idx) : -1;
    };

    // ------------------------------------------------------------ Body parts ----
    if (header->num_bodyparts <= 0 || header->bodypart_index <= 0 ||
        !fits(static_cast<size_t>(header->bodypart_index),
              static_cast<size_t>(header->num_bodyparts), sizeof(StudioBodyPart), mdl_bytes.size())) {
        return result; // skeleton-only model, or a corrupt bodypart table
    }

    const auto* bodyparts = reinterpret_cast<const StudioBodyPart*>(
        mdl_bytes.data() + header->bodypart_index);

    // One surface per texture, so the mesh ends up with as few draw calls as the
    // source material set allows.
    std::unordered_map<int32_t, ir::IRSurface> surface_map;
    std::unordered_map<int32_t, std::unordered_map<uint64_t, uint32_t>> dedup;

    for (int32_t bp = 0; bp < header->num_bodyparts; ++bp) {
        const auto& body = bodyparts[bp];

        // Only the first model of each body part is imported: the remaining ones are
        // alternates selected at runtime by the "body" value (e.g. weapon variants),
        // and importing all of them would stack overlapping geometry.
        if (body.num_models <= 0 || body.model_index <= 0 ||
            !fits(static_cast<size_t>(body.model_index), 1, sizeof(StudioModel), mdl_bytes.size())) {
            continue;
        }

        const auto* model = reinterpret_cast<const StudioModel*>(mdl_bytes.data() + body.model_index);

        if (model->num_verts <= 0 || model->vert_index <= 0 ||
            !fits(static_cast<size_t>(model->vert_index),
                  static_cast<size_t>(model->num_verts) * 3, sizeof(float), mdl_bytes.size())) {
            continue;
        }

        const auto* verts = reinterpret_cast<const float*>(mdl_bytes.data() + model->vert_index);
        const size_t num_verts = static_cast<size_t>(model->num_verts);

        const float* norms = nullptr;
        size_t num_norms = 0;
        if (model->num_norms > 0 && model->norm_index > 0 &&
            fits(static_cast<size_t>(model->norm_index),
                 static_cast<size_t>(model->num_norms) * 3, sizeof(float), mdl_bytes.size())) {
            norms = reinterpret_cast<const float*>(mdl_bytes.data() + model->norm_index);
            num_norms = static_cast<size_t>(model->num_norms);
        }

        // One bone index per vertex; vertices are stored in that bone's local space.
        const uint8_t* vert_bones = nullptr;
        if (model->vert_info_index > 0 &&
            fits(static_cast<size_t>(model->vert_info_index), num_verts, 1, mdl_bytes.size())) {
            vert_bones = reinterpret_cast<const uint8_t*>(mdl_bytes.data() + model->vert_info_index);
        }

        const uint8_t* norm_bones = nullptr;
        if (model->norm_info_index > 0 && num_norms > 0 &&
            fits(static_cast<size_t>(model->norm_info_index), num_norms, 1, mdl_bytes.size())) {
            norm_bones = reinterpret_cast<const uint8_t*>(mdl_bytes.data() + model->norm_info_index);
        }

        if (model->num_mesh <= 0 || model->mesh_index <= 0 ||
            !fits(static_cast<size_t>(model->mesh_index),
                  static_cast<size_t>(model->num_mesh), sizeof(StudioMesh), mdl_bytes.size())) {
            continue;
        }

        const auto* meshes = reinterpret_cast<const StudioMesh*>(mdl_bytes.data() + model->mesh_index);

        for (int32_t m = 0; m < model->num_mesh; ++m) {
            const auto& mesh = meshes[m];
            if (mesh.tri_index <= 0 || static_cast<size_t>(mesh.tri_index) >= mdl_bytes.size()) {
                continue;
            }

            const int32_t tex_index = resolve_texture(mesh.skin_ref);

            float tex_w = 64.0f;
            float tex_h = 64.0f;
            std::string tex_name = "mdl_texture_" + std::to_string(mesh.skin_ref);
            if (tex_index >= 0 && textures) {
                const auto& td = textures[tex_index];
                if (td.width > 0 && td.height > 0) {
                    tex_w = static_cast<float>(td.width);
                    tex_h = static_cast<float>(td.height);
                }
                tex_name.assign(td.name, strnlen(td.name, sizeof(td.name)));
            }

            auto& surf = surface_map[tex_index];
            auto& surf_dedup = dedup[tex_index];
            if (surf.material_name.empty()) {
                surf.material_name = tex_name;
                surf.embedded_texture_index =
                    (tex_index >= 0 && static_cast<size_t>(tex_index) < result.mesh_data.embedded_textures.size() &&
                     result.mesh_data.embedded_textures[tex_index].is_valid())
                        ? tex_index : -1;
            }

            // Append a corner, reusing an identical one already emitted.
            auto push_corner = [&](const TriCorner& c) -> uint32_t {
                const uint64_t key = corner_key(c);
                if (auto it = surf_dedup.find(key); it != surf_dedup.end()) {
                    return it->second;
                }

                const size_t vi = static_cast<size_t>(static_cast<uint16_t>(c.vertex));
                godot::Vector3 pos_local(0, 0, 0);
                if (vi < num_verts) {
                    pos_local = godot::Vector3(verts[vi * 3 + 0], verts[vi * 3 + 1], verts[vi * 3 + 2]);
                }

                // Bone-local -> model space, still Z-up.
                if (vert_bones && vi < num_verts) {
                    const size_t bone = vert_bones[vi];
                    if (bone < bone_rest.size()) {
                        pos_local = bone_rest[bone].xform(pos_local);
                    }
                }

                godot::Vector3 normal_local(0, 0, 1);
                const size_t ni = static_cast<size_t>(static_cast<uint16_t>(c.normal));
                if (norms && ni < num_norms) {
                    normal_local = godot::Vector3(norms[ni * 3 + 0], norms[ni * 3 + 1], norms[ni * 3 + 2]);
                    if (norm_bones) {
                        const size_t bone = norm_bones[ni];
                        if (bone < bone_rest.size()) {
                            // Rotate only: a normal must not pick up the translation.
                            normal_local = bone_rest[bone].basis.xform(normal_local);
                        }
                    }
                }

                const uint32_t index = static_cast<uint32_t>(surf.positions.size());
                surf.positions.push_back(math::source_to_godot(pos_local));
                surf.normals.push_back(math::transform_normal_zup_to_yup(normal_local));
                surf.uv0.emplace_back(static_cast<float>(c.s) / tex_w,
                                      static_cast<float>(c.t) / tex_h);

                // Always push, so the arrays stay parallel with positions. GoldSrc is
                // rigid-skinned: exactly one bone per vertex at full weight.
                const int32_t bone_id = (vert_bones && vi < num_verts)
                                      ? static_cast<int32_t>(vert_bones[vi]) : 0;
                surf.bone_indices.push_back({bone_id, 0, 0, 0});
                surf.bone_weights.push_back({1.0f, 0.0f, 0.0f, 0.0f});

                surf_dedup.emplace(key, index);
                return index;
            };

            // Triangle command stream: an int16 count, then abs(count) corners of four
            // int16 each. A negative count is a fan, positive is a strip, 0 terminates.
            size_t cursor = static_cast<size_t>(mesh.tri_index);
            std::vector<TriCorner> run;

            while (true) {
                if (!fits(cursor, 1, sizeof(int16_t), mdl_bytes.size())) break;

                int16_t cmd = 0;
                std::memcpy(&cmd, mdl_bytes.data() + cursor, sizeof(int16_t));
                cursor += sizeof(int16_t);
                if (cmd == kTriCommandEnd) break;

                const bool is_fan = cmd < 0;
                // Promote before negating: -INT16_MIN does not fit in int16_t.
                const size_t count = static_cast<size_t>(is_fan ? -static_cast<int>(cmd)
                                                                : static_cast<int>(cmd));

                if (!fits(cursor, count * 4, sizeof(int16_t), mdl_bytes.size())) break;

                run.clear();
                run.reserve(count);
                for (size_t k = 0; k < count; ++k) {
                    TriCorner c;
                    std::memcpy(&c.vertex, mdl_bytes.data() + cursor + 0, sizeof(int16_t));
                    std::memcpy(&c.normal, mdl_bytes.data() + cursor + 2, sizeof(int16_t));
                    std::memcpy(&c.s,      mdl_bytes.data() + cursor + 4, sizeof(int16_t));
                    std::memcpy(&c.t,      mdl_bytes.data() + cursor + 6, sizeof(int16_t));
                    cursor += 4 * sizeof(int16_t);
                    run.push_back(c);
                }

                if (run.size() < 3) continue;

                if (is_fan) {
                    const uint32_t hub = push_corner(run[0]);
                    for (size_t k = 1; k + 1 < run.size(); ++k) {
                        surf.indices.push_back(hub);
                        surf.indices.push_back(push_corner(run[k]));
                        surf.indices.push_back(push_corner(run[k + 1]));
                    }
                } else {
                    for (size_t k = 0; k + 2 < run.size(); ++k) {
                        // Strips alternate orientation; swapping the first two corners
                        // on odd triangles keeps the winding consistent, matching the
                        // GL_TRIANGLE_STRIP convention the original renderer used.
                        const uint32_t a = push_corner(run[k]);
                        const uint32_t b = push_corner(run[k + 1]);
                        const uint32_t c = push_corner(run[k + 2]);
                        if (k % 2 == 0) {
                            surf.indices.push_back(a);
                            surf.indices.push_back(b);
                            surf.indices.push_back(c);
                        } else {
                            surf.indices.push_back(b);
                            surf.indices.push_back(a);
                            surf.indices.push_back(c);
                        }
                    }
                }
            }
        }
    }

    for (auto& [tex_idx, surf] : surface_map) {
        if (surf.positions.empty() || surf.indices.empty()) continue;
        result.mesh_data.surfaces.push_back(std::move(surf));
    }

    return result;
}

} // namespace quebratsk::parsers::goldsrc
