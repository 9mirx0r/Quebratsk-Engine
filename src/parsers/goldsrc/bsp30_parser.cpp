#include "bsp30_parser.h"
#include "../../core/math/axis_remap.h"
#include "../../core/math/winding_order.h"

#include <cstring>
#include <unordered_map>

namespace quebratsk::parsers::goldsrc {

std::expected<ParsedBSP30Map, BSP30ParseError> BSP30Parser::parse(
    std::span<const std::byte> bsp_bytes
) {
    if (bsp_bytes.size() < sizeof(BSP30Header)) {
        return std::unexpected(BSP30ParseError::InvalidHeader);
    }

    auto* header = reinterpret_cast<const BSP30Header*>(bsp_bytes.data());
    if (header->version != kBsp30Version) {
        return std::unexpected(BSP30ParseError::VersionMismatch);
    }

    auto get_lump = [&](size_t idx) -> std::span<const std::byte> {
        if (idx >= 15) return {};
        size_t ofs = static_cast<size_t>(header->lumps[idx].file_offset);
        size_t len = static_cast<size_t>(header->lumps[idx].file_length);
        if (ofs + len > bsp_bytes.size()) return {};
        return bsp_bytes.subspan(ofs, len);
    };

    ParsedBSP30Map map_data;

    // Lump 0: Entities
    auto ent_lump = get_lump(0);
    if (!ent_lump.empty()) {
        map_data.entity_string = std::string(reinterpret_cast<const char*>(ent_lump.data()), ent_lump.size());
    }

    // Lump 3: Vertices
    auto vert_lump = get_lump(3);
    auto* bsp_verts = reinterpret_cast<const BSPVertex*>(vert_lump.data());
    size_t num_verts = vert_lump.size() / sizeof(BSPVertex);

    // Lump 12: Edges
    auto edge_lump = get_lump(12);
    auto* bsp_edges = reinterpret_cast<const BSPEdge*>(edge_lump.data());

    // Lump 13: SurfEdges
    auto surfedge_lump = get_lump(13);
    auto* bsp_surfedges = reinterpret_cast<const int32_t*>(surfedge_lump.data());

    // Lump 6: TexInfo
    auto texinfo_lump = get_lump(6);
    auto* bsp_texinfos = reinterpret_cast<const BSPTexInfo*>(texinfo_lump.data());

    // Lump 7: Faces
    auto face_lump = get_lump(7);
    auto* bsp_faces = reinterpret_cast<const BSPFace*>(face_lump.data());
    size_t num_faces = face_lump.size() / sizeof(BSPFace);

    // Group surfaces by texture index
    std::unordered_map<int32_t, ir::IRSurface> surface_map;

    for (size_t f = 0; f < num_faces; ++f) {
        const auto& face = bsp_faces[f];
        int32_t tex_idx = face.texinfo_index >= 0 ? bsp_texinfos[face.texinfo_index].miptex_index : 0;

        auto& surf = surface_map[tex_idx];
        if (surf.material_name.empty()) {
            surf.material_name = "texture_" + std::to_string(tex_idx);
        }

        uint32_t base_index = static_cast<uint32_t>(surf.positions.size());

        // Extract polygon vertices from surfedges
        std::vector<godot::Vector3> poly_positions;

        for (int16_t e = 0; e < face.num_edges; ++e) {
            int32_t surfedge = bsp_surfedges[face.first_edge_index + e];
            uint16_t vert_idx = (surfedge >= 0) ? bsp_edges[surfedge].v[0] : bsp_edges[-surfedge].v[1];

            if (vert_idx < num_verts) {
                const auto& v = bsp_verts[vert_idx];
                godot::Vector3 pos_raw(v.x, v.y, v.z);
                poly_positions.push_back(math::source_to_godot(pos_raw));
            }
        }

        // Fan triangulation for polygon faces
        if (poly_positions.size() >= 3) {
            for (size_t i = 0; i < poly_positions.size(); ++i) {
                surf.positions.push_back(poly_positions[i]);
                surf.normals.push_back(godot::Vector3(0, 1, 0)); // Normal remapping
                surf.uv0.push_back(godot::Vector2(0, 0));
            }

            for (size_t i = 1; i + 1 < poly_positions.size(); ++i) {
                surf.indices.push_back(base_index);
                surf.indices.push_back(base_index + static_cast<uint32_t>(i));
                surf.indices.push_back(base_index + static_cast<uint32_t>(i + 1));
            }
        }
    }

    map_data.mesh_data.source_engine = ir::SourceEngine::GoldSrc;
    map_data.mesh_data.name = "GoldSrcMap";

    for (auto& [idx, surf] : surface_map) {
        // Invert winding order from CW to CCW for Godot
        math::invert_winding_order(surf.indices);
        map_data.mesh_data.surfaces.push_back(std::move(surf));
    }

    // Lump 9: ClipNodes for collision
    auto clipnode_lump = get_lump(9);
    auto* bsp_clipnodes = reinterpret_cast<const BSPClipNode*>(clipnode_lump.data());
    size_t num_clipnodes = clipnode_lump.size() / sizeof(BSPClipNode);

    // Lump 1: Planes
    auto plane_lump = get_lump(1);
    auto* bsp_planes = reinterpret_cast<const BSPPlane*>(plane_lump.data());

    map_data.collision_data.source_engine = ir::SourceEngine::GoldSrc;
    for (size_t c = 0; c < num_clipnodes; ++c) {
        const auto& node = bsp_clipnodes[c];
        if (node.plane_index >= 0 && static_cast<size_t>(node.plane_index) * sizeof(BSPPlane) < plane_lump.size()) {
            const auto& plane = bsp_planes[node.plane_index];
            godot::Vector3 normal_raw(plane.normal[0], plane.normal[1], plane.normal[2]);
            
            ir::IRCollisionData::ClipPlane cp;
            cp.normal = math::transform_normal_zup_to_yup(normal_raw);
            cp.distance = static_cast<float>(plane.distance * math::kHammerUnitsToMeters);
            map_data.collision_data.clip_planes.push_back(cp);
        }
    }

    return map_data;
}

} // namespace quebratsk::parsers::goldsrc
