#include "mdl_source_parser.h"
#include "vvd_parser.h"
#include "../../core/math/axis_remap.h"

#include <cstring>
#include <unordered_map>
#include <vector>

namespace quebratsk::parsers::source1 {

namespace {

/// Overflow-safe "do `count` elements of `elem_size` fit at `offset`?".
bool fits(size_t offset, size_t count, size_t elem_size, size_t total) {
    if (offset > total) return false;
    if (elem_size == 0) return count == 0;
    return count <= (total - offset) / elem_size;
}

/// Read a NUL-terminated string without scanning past the end of the buffer.
std::string read_cstr(std::span<const std::byte> data, size_t offset) {
    if (offset >= data.size()) return {};
    const char* base = reinterpret_cast<const char*>(data.data());
    const size_t max_len = data.size() - offset;
    const size_t len = ::strnlen(base + offset, max_len);
    if (len >= max_len) return {}; // no terminator inside the buffer
    return std::string(base + offset, len);
}

/// Every *_offset in a VTX file is relative to the address of the struct holding it,
/// never to the start of the file. This helper makes that explicit at each use.
template <typename T>
const T* at_relative(std::span<const std::byte> data, size_t struct_offset, int32_t rel,
                     size_t count = 1) {
    if (rel < 0) return nullptr;
    const size_t abs = struct_offset + static_cast<size_t>(rel);
    if (!fits(abs, count, sizeof(T), data.size())) return nullptr;
    return reinterpret_cast<const T*>(data.data() + abs);
}

/// Bone hierarchy is not needed here: unlike GoldSrc, Source stores vertices already in
/// model space, so positions can be used directly.
void fill_skeleton(std::span<const std::byte> mdl, const SourceStudioHeader* header,
                   ir::IRSkeletonData& out) {
    if (header->num_bones <= 0 || header->bone_index <= 0 ||
        !fits(static_cast<size_t>(header->bone_index),
              static_cast<size_t>(header->num_bones), sizeof(SourceStudioBone), mdl.size())) {
        return;
    }

    const auto* bones = reinterpret_cast<const SourceStudioBone*>(mdl.data() + header->bone_index);

    for (int32_t i = 0; i < header->num_bones; ++i) {
        const auto& b = bones[i];
        ir::IRBone ir_b;

        // name_index is relative to the start of this bone record.
        const size_t bone_base = static_cast<size_t>(header->bone_index) +
                                 static_cast<size_t>(i) * sizeof(SourceStudioBone);
        bool named = false;
        if (b.name_index > 0 && bone_base < mdl.size() &&
            static_cast<size_t>(b.name_index) < mdl.size() - bone_base) {
            std::string name = read_cstr(mdl, bone_base + static_cast<size_t>(b.name_index));
            if (!name.empty()) {
                ir_b.name = std::move(name);
                named = true;
            }
        }
        if (!named) ir_b.name = "ValveBiped.Bip01_Bone_" + std::to_string(i);

        ir_b.parent_index = b.parent;
        ir_b.position = math::source_to_godot(godot::Vector3(b.pos[0], b.pos[1], b.pos[2]));
        ir_b.rotation = math::source_quat_to_godot(
            godot::Quaternion(b.rot[0], b.rot[1], b.rot[2], b.rot[3]));

        out.bones.push_back(std::move(ir_b));
    }
}

} // namespace

std::expected<ParsedSourceMDLModel, SourceMDLParseError> SourceMDLParser::parse(
    std::span<const std::byte> mdl_bytes
) {
    return parse_bundle(SourceModelBundle{mdl_bytes, {}, {}});
}

std::expected<ParsedSourceMDLModel, SourceMDLParseError> SourceMDLParser::parse_bundle(
    const SourceModelBundle& bundle
) {
    const std::span<const std::byte> mdl = bundle.mdl;

    if (mdl.size() < sizeof(SourceStudioHeader)) {
        return std::unexpected(SourceMDLParseError::InvalidHeader);
    }

    const auto* header = reinterpret_cast<const SourceStudioHeader*>(mdl.data());
    if (std::memcmp(header->magic, kSourceMdlMagic.data(), 4) != 0) {
        return std::unexpected(SourceMDLParseError::InvalidHeader);
    }

    // GoldSrc StudioMDL uses the SAME "IDST" magic, so the magic alone cannot tell the
    // two apart. Without this check a GoldSrc model was accepted here, returned an empty
    // mesh, and the caller never fell through to MDL10Parser.
    if (header->version < kSourceMdlMinVersion || header->version > kSourceMdlMaxVersion) {
        return std::unexpected(SourceMDLParseError::VersionMismatch);
    }

    ParsedSourceMDLModel result;
    result.mesh_data.source_engine = ir::SourceEngine::Source1;
    result.mesh_data.name = std::string(header->name, ::strnlen(header->name, sizeof(header->name)));
    result.skeleton_data.source_engine = ir::SourceEngine::Source1;
    result.skeleton_data.name = result.mesh_data.name;

    fill_skeleton(mdl, header, result.skeleton_data);

    // Without the companions there is nothing more to recover: a .mdl has no vertices.
    if (bundle.vvd.empty() || bundle.vtx.empty()) {
        return result;
    }

    // ------------------------------------------------------------------ .vvd ----
    auto vertices_res = VVDParser::load_vertices(bundle.vvd, 0);
    if (!vertices_res.has_value()) {
        return std::unexpected(SourceMDLParseError::CorruptedData);
    }
    const std::vector<VVDVertex>& vertices = vertices_res.value();
    if (vertices.empty()) {
        return result;
    }

    // The three files are only consistent as a set; a mismatched checksum means the
    // caller paired a .mdl with someone else's .vvd/.vtx.
    if (auto vvd_sum = VVDParser::read_checksum(bundle.vvd);
        vvd_sum.has_value() && *vvd_sum != header->checksum) {
        return std::unexpected(SourceMDLParseError::ChecksumMismatch);
    }

    // ------------------------------------------------------------------ .vtx ----
    const std::span<const std::byte> vtx = bundle.vtx;
    if (vtx.size() < sizeof(VTXHeader)) {
        return std::unexpected(SourceMDLParseError::CorruptedData);
    }
    const auto* vtx_header = reinterpret_cast<const VTXHeader*>(vtx.data());
    if (vtx_header->version != kVtxVersion) {
        return std::unexpected(SourceMDLParseError::VersionMismatch);
    }
    if (vtx_header->checksum != header->checksum) {
        return std::unexpected(SourceMDLParseError::ChecksumMismatch);
    }

    // -------------------------------------------------------- material names ----
    std::vector<std::string> material_names;
    if (header->num_textures > 0 && header->texture_index > 0 &&
        fits(static_cast<size_t>(header->texture_index),
             static_cast<size_t>(header->num_textures), sizeof(SourceTexture), mdl.size())) {

        const auto* textures = reinterpret_cast<const SourceTexture*>(mdl.data() + header->texture_index);
        material_names.reserve(static_cast<size_t>(header->num_textures));

        for (int32_t i = 0; i < header->num_textures; ++i) {
            const size_t base = static_cast<size_t>(header->texture_index) +
                                static_cast<size_t>(i) * sizeof(SourceTexture);
            std::string name;
            if (textures[i].name_index > 0) {
                name = read_cstr(mdl, base + static_cast<size_t>(textures[i].name_index));
            }
            material_names.push_back(name.empty() ? ("material_" + std::to_string(i)) : name);
        }
    }

    // ------------------------------------------------- body parts / geometry ----
    if (header->num_bodyparts <= 0 || header->bodypart_index <= 0 ||
        !fits(static_cast<size_t>(header->bodypart_index),
              static_cast<size_t>(header->num_bodyparts), sizeof(SourceBodyPart), mdl.size())) {
        return result;
    }
    if (vtx_header->num_body_parts <= 0) {
        return result;
    }

    std::unordered_map<int32_t, ir::IRSurface> surface_map;

    const int32_t body_part_count = header->num_bodyparts < vtx_header->num_body_parts
                                  ? header->num_bodyparts : vtx_header->num_body_parts;

    for (int32_t bp = 0; bp < body_part_count; ++bp) {
        const size_t mdl_bp_ofs = static_cast<size_t>(header->bodypart_index) +
                                  static_cast<size_t>(bp) * sizeof(SourceBodyPart);
        const auto* mdl_bp = reinterpret_cast<const SourceBodyPart*>(mdl.data() + mdl_bp_ofs);

        const size_t vtx_bp_ofs = static_cast<size_t>(vtx_header->body_part_offset) +
                                  static_cast<size_t>(bp) * sizeof(VTXBodyPart);
        const auto* vtx_bp = at_relative<VTXBodyPart>(vtx, 0, static_cast<int32_t>(vtx_bp_ofs));
        if (!vtx_bp) continue;

        // Only the first model of a body part is imported; the rest are runtime
        // alternates (weapon variants and similar) and stacking them would overlap.
        if (mdl_bp->num_models <= 0 || vtx_bp->num_models <= 0) continue;

        const auto* mdl_model = at_relative<SourceModel>(mdl, mdl_bp_ofs, mdl_bp->model_index);
        const auto* vtx_model = at_relative<VTXModel>(vtx, vtx_bp_ofs, vtx_bp->model_offset);
        if (!mdl_model || !vtx_model) continue;

        // LOD 0 is the highest detail level.
        if (vtx_model->num_lods <= 0) continue;
        const size_t vtx_model_ofs = vtx_bp_ofs + static_cast<size_t>(vtx_bp->model_offset);
        const auto* vtx_lod = at_relative<VTXModelLOD>(vtx, vtx_model_ofs, vtx_model->lod_offset);
        if (!vtx_lod) continue;

        if (mdl_model->num_meshes <= 0 || vtx_lod->num_meshes <= 0) continue;

        const size_t mdl_model_ofs = mdl_bp_ofs + static_cast<size_t>(mdl_bp->model_index);
        const size_t vtx_lod_ofs = vtx_model_ofs + static_cast<size_t>(vtx_model->lod_offset);

        // A model's vertices start at a BYTE offset into the .vvd array.
        const size_t model_vertex_base =
            static_cast<size_t>(mdl_model->vertex_index) / sizeof(VVDVertex);

        const int32_t mesh_count = mdl_model->num_meshes < vtx_lod->num_meshes
                                 ? mdl_model->num_meshes : vtx_lod->num_meshes;

        for (int32_t mi = 0; mi < mesh_count; ++mi) {
            const auto* mdl_mesh = at_relative<SourceMesh>(
                mdl, mdl_model_ofs, mdl_model->mesh_index + static_cast<int32_t>(mi * sizeof(SourceMesh)));
            const auto* vtx_mesh = at_relative<VTXMesh>(
                vtx, vtx_lod_ofs, vtx_lod->mesh_offset + static_cast<int32_t>(mi * sizeof(VTXMesh)));
            if (!mdl_mesh || !vtx_mesh) continue;

            const int32_t material = mdl_mesh->material;
            auto& surf = surface_map[material];
            if (surf.material_name.empty()) {
                surf.material_name =
                    (material >= 0 && static_cast<size_t>(material) < material_names.size())
                        ? material_names[material]
                        : ("material_" + std::to_string(material));
            }

            const size_t vtx_mesh_ofs = vtx_lod_ofs + static_cast<size_t>(vtx_lod->mesh_offset) +
                                        static_cast<size_t>(mi) * sizeof(VTXMesh);

            // Absolute index of this mesh's first vertex in the flattened .vvd array.
            const size_t mesh_vertex_base = model_vertex_base +
                                            static_cast<size_t>(mdl_mesh->vertex_offset);

            for (int32_t sg = 0; sg < vtx_mesh->num_strip_groups; ++sg) {
                const auto* group = at_relative<VTXStripGroup>(
                    vtx, vtx_mesh_ofs,
                    vtx_mesh->strip_group_header_offset + static_cast<int32_t>(sg * sizeof(VTXStripGroup)));
                if (!group) continue;

                const size_t group_ofs = vtx_mesh_ofs +
                                         static_cast<size_t>(vtx_mesh->strip_group_header_offset) +
                                         static_cast<size_t>(sg) * sizeof(VTXStripGroup);

                if (group->num_verts <= 0 || group->num_indices <= 0) continue;

                const auto* group_verts = at_relative<VTXVertex>(
                    vtx, group_ofs, group->vert_offset, static_cast<size_t>(group->num_verts));
                const auto* group_indices = at_relative<uint16_t>(
                    vtx, group_ofs, group->index_offset, static_cast<size_t>(group->num_indices));
                if (!group_verts || !group_indices) continue;

                // Map a strip-group vertex onto the surface, reusing it if already added.
                std::unordered_map<uint16_t, uint32_t> emitted;
                auto emit_vertex = [&](uint16_t group_vert_idx) -> int64_t {
                    if (group_vert_idx >= group->num_verts) return -1;

                    if (auto it = emitted.find(group_vert_idx); it != emitted.end()) {
                        return it->second;
                    }

                    // Three hops: strip index -> strip-group Vertex_t -> the mesh's
                    // vertex range in the flattened .vvd array.
                    const size_t vvd_index =
                        mesh_vertex_base + group_verts[group_vert_idx].orig_mesh_vert_id;
                    if (vvd_index >= vertices.size()) return -1;

                    const VVDVertex& v = vertices[vvd_index];
                    const uint32_t out_index = static_cast<uint32_t>(surf.positions.size());

                    // Source already stores vertices in model space; unlike GoldSrc no
                    // bone transform has to be applied here.
                    surf.positions.push_back(math::source_to_godot(
                        godot::Vector3(v.position[0], v.position[1], v.position[2])));
                    surf.normals.push_back(math::transform_normal_zup_to_yup(
                        godot::Vector3(v.normal[0], v.normal[1], v.normal[2])));
                    surf.uv0.emplace_back(v.tex_coord[0], v.tex_coord[1]);

                    std::array<int32_t, 4> bone_idx{0, 0, 0, 0};
                    std::array<float, 4> bone_w{0.0f, 0.0f, 0.0f, 0.0f};
                    const uint8_t influences = v.bone_weights.num_bones <= 3
                                             ? v.bone_weights.num_bones : uint8_t{3};
                    for (uint8_t k = 0; k < influences; ++k) {
                        bone_idx[k] = static_cast<int32_t>(v.bone_weights.bone[k]);
                        bone_w[k] = v.bone_weights.weight[k];
                    }
                    if (influences == 0) bone_w[0] = 1.0f;
                    surf.bone_indices.push_back(bone_idx);
                    surf.bone_weights.push_back(bone_w);

                    emitted.emplace(group_vert_idx, out_index);
                    return out_index;
                };

                for (int32_t st = 0; st < group->num_strips; ++st) {
                    const auto* strip = at_relative<VTXStrip>(
                        vtx, group_ofs, group->strip_offset + static_cast<int32_t>(st * sizeof(VTXStrip)));
                    if (!strip || strip->num_indices < 3) continue;

                    if (strip->index_offset < 0 ||
                        static_cast<size_t>(strip->index_offset) + static_cast<size_t>(strip->num_indices)
                            > static_cast<size_t>(group->num_indices)) {
                        continue;
                    }

                    const uint16_t* strip_indices = group_indices + strip->index_offset;

                    if (strip->flags & kVtxStripIsTriStrip) {
                        for (int32_t k = 0; k + 2 < strip->num_indices; ++k) {
                            const int64_t a = emit_vertex(strip_indices[k]);
                            const int64_t b = emit_vertex(strip_indices[k + 1]);
                            const int64_t c = emit_vertex(strip_indices[k + 2]);
                            if (a < 0 || b < 0 || c < 0) continue;
                            // Strips alternate orientation; swap on odd triangles to
                            // keep the winding consistent.
                            if (k % 2 == 0) {
                                surf.indices.push_back(static_cast<uint32_t>(a));
                                surf.indices.push_back(static_cast<uint32_t>(b));
                                surf.indices.push_back(static_cast<uint32_t>(c));
                            } else {
                                surf.indices.push_back(static_cast<uint32_t>(b));
                                surf.indices.push_back(static_cast<uint32_t>(a));
                                surf.indices.push_back(static_cast<uint32_t>(c));
                            }
                        }
                    } else {
                        // Plain triangle list.
                        for (int32_t k = 0; k + 2 < strip->num_indices; k += 3) {
                            const int64_t a = emit_vertex(strip_indices[k]);
                            const int64_t b = emit_vertex(strip_indices[k + 1]);
                            const int64_t c = emit_vertex(strip_indices[k + 2]);
                            if (a < 0 || b < 0 || c < 0) continue;
                            surf.indices.push_back(static_cast<uint32_t>(a));
                            surf.indices.push_back(static_cast<uint32_t>(b));
                            surf.indices.push_back(static_cast<uint32_t>(c));
                        }
                    }
                }
            }
        }
    }

    for (auto& [material, surf] : surface_map) {
        if (surf.positions.empty() || surf.indices.empty()) continue;
        result.mesh_data.surfaces.push_back(std::move(surf));
    }

    return result;
}

} // namespace quebratsk::parsers::source1
