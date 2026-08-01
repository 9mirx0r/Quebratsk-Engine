#include "bsp30_parser.h"
#include "../../core/io/byte_reader.h"
#include "../../core/math/axis_remap.h"
#include "structs/wad3_structs.h" // MiptexHeader, shared with the WAD3 reader
#include "wad3_parser.h"

#include <cctype>

#include <godot_cpp/variant/utility_functions.hpp>          // reuse the miptex decoder for embedded textures

#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace quebratsk::parsers::goldsrc {

namespace {

/// Whether a face wearing this texture should be left out of the mesh.
///
/// A GoldSrc map carries brushes the engine never draws. Some mark volumes rather than
/// surfaces: AAATRIGGER is what a trigger looks like in the editor, CLIP is a wall that
/// stops players and not bullets, ORIGIN sets a rotating brush's pivot. Others are
/// instructions to the compiler that should not have survived it at all.
///
/// Drawing them is not a small cosmetic error. AAATRIGGER is a magenta tile with a lambda
/// on it, and a map with a few trigger volumes in it comes out with solid magenta slabs
/// standing in the middle of the level, walling off rooms that are open in the game.
///
/// Names are compared in lower case because a mapper writes them however they please.
bool is_non_visible_texture(const std::string& name) {
    std::string lower;
    lower.reserve(name.size());
    for (const char c : name) {
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }

    // The skybox is drawn behind these rather than on them.
    if (lower == "sky" || lower.starts_with("sky_")) return true;

    static constexpr const char* kToolTextures[] = {
        "aaatrigger",  // trigger volumes
        "clip",        // blocks players, not projectiles
        "clipbevel", "clipbevelbrush", "cliphull1", "cliphull2", "cliphull3",
        "origin",      // pivot for a rotating brush
        "hint", "skip", "solidhint",  // compiler instructions
        "null",        // face removed at compile time
        "bevel",
        "nodraw",      // Source's name for the same idea
        "trigger",
        "contentwater", "contentempty", "contentsolid",
    };
    for (const char* tool : kToolTextures) {
        if (lower == tool) return true;
    }
    return false;
}

} // namespace

std::expected<ParsedBSP30Map, BSP30ParseError> BSP30Parser::parse(
    std::span<const std::byte> bsp_bytes
) {
    const io::ByteReader bsp(bsp_bytes);

    const auto header_span = bsp.array_at<BSP30Header>(0, 1);
    if (header_span.empty()) {
        return std::unexpected(BSP30ParseError::InvalidHeader);
    }
    const BSP30Header* header = header_span.data();

    if (header->version != kBsp30Version) {
        return std::unexpected(BSP30ParseError::VersionMismatch);
    }

    // Lump offsets and lengths are int32_t on disk, so a negative value would cast to a
    // huge size_t. bytes_at() rejects any range that does not fit entirely, returning an
    // empty span rather than the undefined behaviour subspan() would give.
    auto get_lump = [&](size_t idx) -> std::span<const std::byte> {
        if (idx >= 15) return {};
        const int32_t raw_ofs = header->lumps[idx].file_offset;
        const int32_t raw_len = header->lumps[idx].file_length;
        if (raw_ofs < 0 || raw_len < 0) return {};
        return bsp.bytes_at(static_cast<size_t>(raw_ofs), static_cast<size_t>(raw_len));
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
    size_t num_edges = edge_lump.size() / sizeof(BSPEdge);

    // Lump 13: SurfEdges
    auto surfedge_lump = get_lump(13);
    auto* bsp_surfedges = reinterpret_cast<const int32_t*>(surfedge_lump.data());
    size_t num_surfedges = surfedge_lump.size() / sizeof(int32_t);

    // Lump 6: TexInfo
    auto texinfo_lump = get_lump(6);
    auto* bsp_texinfos = reinterpret_cast<const BSPTexInfo*>(texinfo_lump.data());
    size_t num_texinfos = texinfo_lump.size() / sizeof(BSPTexInfo);

    // Lump 7: Faces
    auto face_lump = get_lump(7);
    auto* bsp_faces = reinterpret_cast<const BSPFace*>(face_lump.data());
    size_t num_faces = face_lump.size() / sizeof(BSPFace);

    // Lump 1: Planes — needed here for per-face normals, and again below for collision.
    auto plane_lump = get_lump(1);
    auto* bsp_planes = reinterpret_cast<const BSPPlane*>(plane_lump.data());
    size_t num_planes = plane_lump.size() / sizeof(BSPPlane);

    // Lump 2: Texture directory. Needed for two things the parser previously skipped:
    // the real texture name (so the material can be resolved against mounted WADs) and
    // the texture dimensions, without which the S/T projection vectors cannot be turned
    // into normalized UVs.
    struct MipTexEntry {
        std::string name;
        uint32_t width = 64;   // fallback keeps UVs finite for a corrupt directory
        uint32_t height = 64;
        int32_t embedded_index = -1; // into map_data.mesh_data.embedded_textures
    };
    std::vector<MipTexEntry> miptextures;
    int diag_embedded = 0;
    int diag_external = 0;
    int diag_sky_faces = 0;
    bool entry_was_embedded = false;

    {
        io::ByteReader tex(get_lump(2));
        if (const auto lump_hdr = tex.read<BSPMipTexLumpHeader>(); lump_hdr.has_value()) {
            const int32_t count = lump_hdr->num_miptex;
            const auto offsets = tex.read_array<int32_t>(
                count > 0 ? static_cast<size_t>(count) : 0);

            if (!offsets.empty()) {
                miptextures.reserve(offsets.size());
                for (int32_t ofs : offsets) {
                    MipTexEntry entry;

                    // -1 marks a texture that lives entirely in an external WAD.
                    const auto mh_span = ofs >= 0
                        ? tex.array_at<MiptexHeader>(static_cast<size_t>(ofs), 1)
                        : std::span<const MiptexHeader>{};
                    if (!mh_span.empty()) {
                        const MiptexHeader* mh = mh_span.data();
                        entry.name.assign(mh->name, strnlen(mh->name, sizeof(mh->name)));
                        if (mh->width > 0 && mh->height > 0 &&
                            mh->width <= 4096 && mh->height <= 4096) {
                            entry.width = mh->width;
                            entry.height = mh->height;
                        }

                        // A non-zero mip offset means the pixels are stored right here
                        // in the BSP rather than in an external WAD. Most compiled
                        // GoldSrc maps embed their textures, so decoding them is the
                        // difference between a textured map and a blank grey one.
                        entry_was_embedded = mh->offsets[0] != 0;
                        if (mh->offsets[0] != 0) {
                            const auto miptex_span = tex.tail_at(static_cast<size_t>(ofs));
                            if (auto decoded = WAD3Parser::parse_miptex(miptex_span);
                                decoded.has_value() && decoded->is_valid()) {
                                entry.embedded_index =
                                    static_cast<int32_t>(map_data.mesh_data.embedded_textures.size());
                                map_data.mesh_data.embedded_textures.push_back(std::move(decoded.value()));
                            }
                        }
                    }
                    miptextures.push_back(std::move(entry));
                    if (entry_was_embedded) ++diag_embedded; else ++diag_external;
                    entry_was_embedded = false;
                }
            }
        }
    }

    // Group surfaces by texture index
    std::unordered_map<int32_t, ir::IRSurface> surface_map;

    for (size_t f = 0; f < num_faces; ++f) {
        const auto& face = bsp_faces[f];

        // texinfo_index was only checked for >= 0, never against the lump size, so a
        // crafted map read 40 bytes far outside lump 6 (and dereferenced null when the
        // lump was absent).
        const BSPTexInfo* tex_info = nullptr;
        int32_t tex_idx = -1;
        if (face.texinfo_index >= 0 && static_cast<size_t>(face.texinfo_index) < num_texinfos) {
            tex_info = &bsp_texinfos[face.texinfo_index];
            tex_idx = tex_info->miptex_index;
        }

        const MipTexEntry* miptex = nullptr;
        if (tex_idx >= 0 && static_cast<size_t>(tex_idx) < miptextures.size()) {
            miptex = &miptextures[tex_idx];

            // A face textured "sky" is not a surface: GoldSrc draws the skybox through it
            // and never renders the brush. Emitting it as geometry puts the sky texture's
            // own pixels across the level, which is a lurid purple placeholder that no
            // player of the game has ever seen, and walls the map in besides.
            if (is_non_visible_texture(miptex->name)) {
                ++diag_sky_faces;
                continue;
            }
        }

        auto& surf = surface_map[tex_idx];
        if (surf.material_name.empty()) {
            // Use the real WAD texture name so TextureLoader can resolve it against
            // mounted archives. It used to be a synthetic "texture_<n>", which could
            // never match anything in the VFS.
            surf.material_name = (miptex && !miptex->name.empty())
                               ? miptex->name
                               : "texture_" + std::to_string(tex_idx);
            // Prefer the map's own copy; TextureLoader is only the fallback for maps
            // that reference external WADs.
            surf.embedded_texture_index = miptex ? miptex->embedded_index : -1;
        }

        // Face normal comes from its plane, flipped when the face is on the back side.
        // Every vertex previously got a hardcoded (0,1,0), which made all lighting flat.
        godot::Vector3 face_normal(0, 1, 0);
        if (face.plane_index < num_planes) {
            const auto& pl = bsp_planes[face.plane_index];
            godot::Vector3 n(pl.normal[0], pl.normal[1], pl.normal[2]);
            if (face.plane_side != 0) n = -n;
            face_normal = math::transform_normal_zup_to_yup(n);
        }

        const float tex_w = static_cast<float>(miptex ? miptex->width : 64u);
        const float tex_h = static_cast<float>(miptex ? miptex->height : 64u);

        uint32_t base_index = static_cast<uint32_t>(surf.positions.size());

        // Extract polygon vertices from surfedges
        std::vector<godot::Vector3> poly_positions;
        std::vector<godot::Vector2> poly_uvs;

        for (int16_t e = 0; e < face.num_edges; ++e) {
            size_t surfedge_idx = static_cast<size_t>(face.first_edge_index + e);
            if (surfedge_idx >= num_surfedges) break;

            int32_t surfedge = bsp_surfedges[surfedge_idx];
            // Negating INT32_MIN is signed overflow (UB). Take the magnitude in
            // unsigned arithmetic, where wrapping is well defined.
            size_t abs_edge_idx = surfedge >= 0
                ? static_cast<size_t>(static_cast<uint32_t>(surfedge))
                : static_cast<size_t>(0u - static_cast<uint32_t>(surfedge));
            if (abs_edge_idx >= num_edges) continue;

            uint16_t vert_idx = (surfedge >= 0) ? bsp_edges[abs_edge_idx].v[0] : bsp_edges[abs_edge_idx].v[1];

            if (vert_idx < num_verts) {
                const auto& v = bsp_verts[vert_idx];
                godot::Vector3 pos_raw(v.x, v.y, v.z);
                poly_positions.push_back(math::source_to_godot(pos_raw));

                // Planar UV projection. BSPTexInfo carries the S/T axes plus their
                // offsets; dividing by the texture size normalizes texels to 0..1.
                // These vectors are expressed in the map's original Hammer-unit space,
                // so the RAW vertex must be used here, not the Godot-remapped one.
                if (tex_info) {
                    const float u = (pos_raw.x * tex_info->vector_s[0] +
                                     pos_raw.y * tex_info->vector_s[1] +
                                     pos_raw.z * tex_info->vector_s[2] +
                                     tex_info->vector_s[3]) / tex_w;
                    const float t = (pos_raw.x * tex_info->vector_t[0] +
                                     pos_raw.y * tex_info->vector_t[1] +
                                     pos_raw.z * tex_info->vector_t[2] +
                                     tex_info->vector_t[3]) / tex_h;
                    poly_uvs.emplace_back(u, t);
                } else {
                    poly_uvs.emplace_back(0.0f, 0.0f);
                }
            }
        }

        // Fan triangulation for polygon faces
        if (poly_positions.size() >= 3) {
            for (size_t i = 0; i < poly_positions.size(); ++i) {
                surf.positions.push_back(poly_positions[i]);
                surf.normals.push_back(face_normal);
                surf.uv0.push_back(poly_uvs[i]);
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
        // NOTE: no winding inversion here. math::source_to_godot() applies
        //   M = [0 -1 0; 0 0 1; -1 0 0],  det(M) = +1
        // which preserves orientation, so GoldSrc winding is already correct in Godot.
        // Inverting it flips every triangle and renders the whole map inside-out under
        // backface culling.
        //
        // The matrix written here used to be [1 0 0; 0 0 1; 0 -1 0], which was the remap
        // before it was corrected, and the comment outlived it. The conclusion held either
        // way, since both have determinant +1, which is exactly why nobody noticed.
        map_data.mesh_data.surfaces.push_back(std::move(surf));
    }

    // Lump 9: ClipNodes, GoldSrc's real collision. Planes (lump 1) were already loaded above
    // for per-face normals and are reused here.
    //
    // WHAT THIS DOES AND DOES NOT DO. A clipnode is a node in a BSP tree: a splitting plane
    // and two children, where a negative child is a content type rather than another node.
    // Three of those trees exist per map, one per player size, and together they are what a
    // GoldSrc player actually collides against. That is why a player fits through a 33 unit
    // gap that a capsule of the same radius does not: the hull is an axis-aligned box tested
    // against planes, not a swept cylinder.
    //
    // What is collected below is the flat list of splitting planes, with the tree thrown
    // away. A plane list cannot answer "is this point solid", so nothing consumes
    // collision_data and map collision is still built from the visible surfaces by
    // trimesh_body(). Rebuilding the tree needs the model lump's headnode indices, which
    // say where each of the three hulls begins, and those are not read yet.
    //
    // Left as it is rather than half-changed: the visible-surface collider works, and
    // replacing it is a piece of work that should land whole.
    auto clipnode_lump = get_lump(9);
    auto* bsp_clipnodes = reinterpret_cast<const BSPClipNode*>(clipnode_lump.data());
    size_t num_clipnodes = clipnode_lump.size() / sizeof(BSPClipNode);

    map_data.collision_data.source_engine = ir::SourceEngine::GoldSrc;
    for (size_t c = 0; c < num_clipnodes; ++c) {
        const auto& node = bsp_clipnodes[c];
        // Comparing the plane's start offset against the lump size allowed a read that
        // began inside the lump and ran up to 19 bytes past its end. Compare the index
        // against the element count instead.
        if (node.plane_index >= 0 && static_cast<size_t>(node.plane_index) < num_planes) {
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
