#include "bsp30_parser.h"
#include "bsp30_solids.h"

#include <algorithm>
#include "../../core/io/byte_reader.h"
#include "../../core/math/axis_remap.h"
#include "structs/wad3_structs.h" // MiptexHeader, shared with the WAD3 reader
#include "wad3_parser.h"

#include <array>
#include <cctype>
#include <cmath>
#include <limits>

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
/// Which light styles are dark when the level starts.
///
/// A light a mapper gave a name to can be switched, so the compiler bakes it into a grid of
/// its own under a style numbered 32 or more and writes that number back into the light's
/// entity. The game then starts that style lit, unless the light is flagged "initially
/// dark" (spawnflags bit 1), in which case it starts at nothing (the public SDK's
/// lights.cpp). Adding such a grid in shows a room lit by lamps nobody has switched on.
///
/// Styles below 32 are the game's fixed flickers and pulses and are never off.
std::array<bool, 256> styles_starting_dark(const std::string& entities) {
    std::array<bool, 256> dark{};
    const auto value_of = [](const std::string& block, const char* key) -> std::string {
        const std::string quoted = std::string("\"") + key + "\"";
        const size_t at = block.find(quoted);
        if (at == std::string::npos) return {};
        const size_t open = block.find('"', at + quoted.size());
        if (open == std::string::npos) return {};
        const size_t close = block.find('"', open + 1);
        if (close == std::string::npos) return {};
        return block.substr(open + 1, close - open - 1);
    };
    size_t pos = 0;
    while (true) {
        const size_t open = entities.find('{', pos);
        if (open == std::string::npos) break;
        const size_t close = entities.find('}', open);
        if (close == std::string::npos) break;
        pos = close + 1;

        const std::string block = entities.substr(open, close - open);
        if (!value_of(block, "classname").starts_with("light")) continue;
        if (value_of(block, "targetname").empty()) continue;
        const int style = std::atoi(value_of(block, "style").c_str());
        const int flags = std::atoi(value_of(block, "spawnflags").c_str());
        if (style >= 32 && style < 255 && (flags & 1) != 0) dark[static_cast<size_t>(style)] = true;
    }
    return dark;
}

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

/// One light sample covers this many map units along each texture axis.
constexpr float kLuxelUnits = 16.0f;
/// The white patch every unlit face points at, and the point inside it that is sampled.
constexpr uint32_t kWhitePatch = 4;
constexpr float kWhitePoint = 2.0f;
/// Retail faces measure at most 17 samples a side. Anything far past that is a corrupt
/// face rather than a large one, and is left unlit instead of being given an atlas row.
constexpr int kMaxLuxelsPerSide = 256;

/// Lays rectangles out in rows, left to right, starting a new row when one is full.
///
/// Light grids are tiny (17 samples a side at most) and there are thousands of them, so
/// rows waste little and placing one costs nothing. The atlas has a fixed width and grows
/// downwards for as long as there is something to place.
class LightAtlas {
public:
    explicit LightAtlas(uint32_t width) : m_width(width) {}

    /// Top-left corner of a free w x h rectangle. `w` must not exceed the width.
    std::pair<uint32_t, uint32_t> place(uint32_t w, uint32_t h) {
        if (m_cursor_x + w > m_width) {
            m_row_y += m_row_height;
            m_row_height = 0;
            m_cursor_x = 0;
        }
        const std::pair<uint32_t, uint32_t> at(m_cursor_x, m_row_y);
        m_cursor_x += w;
        m_row_height = std::max(m_row_height, h);
        const size_t needed = static_cast<size_t>(m_row_y + m_row_height) * m_width * 4;
        if (m_pixels.size() < needed) m_pixels.resize(needed, 255);
        return at;
    }

    uint8_t* pixel(uint32_t x, uint32_t y) {
        return m_pixels.data() + (static_cast<size_t>(y) * m_width + x) * 4;
    }

    [[nodiscard]] uint32_t width() const { return m_width; }
    [[nodiscard]] uint32_t height() const { return m_row_y + m_row_height; }
    std::vector<uint8_t> take_pixels() { return std::move(m_pixels); }

private:
    uint32_t m_width;
    uint32_t m_cursor_x = 0;
    uint32_t m_row_y = 0;
    uint32_t m_row_height = 0;
    std::vector<uint8_t> m_pixels;   // RGBA8, opaque white until written
};

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

    // Blue Shift stores planes in lump 0 and entities in lump 1; every other retail game
    // stores them the other way round, and nothing else differs. The version number is the
    // same, so the only way to tell is by content: the entities lump is text that opens
    // with '{'. Measured on the retail corpus: all 37 bshift maps are swapped, the other
    // 360 are not (tools/verify/validate_bsp30.py, detect_layout).
    auto starts_with_brace = [](std::span<const std::byte> lump) {
        for (const std::byte b : lump) {
            const auto c = static_cast<unsigned char>(b);
            if (c == 0x20 || c == 0x09 || c == 0x0D || c == 0x0A) continue;
            return c == '{';
        }
        return false;
    };
    size_t entities_lump = 0;
    size_t planes_lump = 1;
    if (!starts_with_brace(get_lump(0)) && starts_with_brace(get_lump(1))) {
        entities_lump = 1;
        planes_lump = 0;
    }
    map_data.blue_shift_layout = entities_lump == 1;

    // Entities
    auto ent_lump = get_lump(entities_lump);
    if (!ent_lump.empty()) {
        map_data.entity_string = std::string(reinterpret_cast<const char*>(ent_lump.data()), ent_lump.size());
    }
    const std::array<bool, 256> style_starts_dark = styles_starting_dark(map_data.entity_string);

    // Which faces each brush model owns, filled from lump 14 and used far below to build one
    // mesh per model instead of one for the whole map.
    std::vector<std::pair<size_t, size_t>> model_ranges;

    // Lump 14: brush models. One per brush entity plus the world, each a box in the map's
    // own units. This is the only record of where a door, a trigger or a body of water is:
    // those entities carry "model" "*N" and no origin at all.
    if (const auto model_lump = get_lump(14); !model_lump.empty()) {
        const size_t count = model_lump.size() / sizeof(BSPModel);
        const auto* models = reinterpret_cast<const BSPModel*>(model_lump.data());
        map_data.brush_models.reserve(count);

        for (size_t i = 0; i < count; ++i) {
            const godot::Vector3 a = math::source_to_godot(
                godot::Vector3(models[i].mins[0], models[i].mins[1], models[i].mins[2]));
            const godot::Vector3 b = math::source_to_godot(
                godot::Vector3(models[i].maxs[0], models[i].maxs[1], models[i].maxs[2]));
            // Re-cornered after the remap: it permutes and negates axes, so a transformed
            // minimum is not necessarily still a minimum.
            BrushModelBounds box;
            box.mins = godot::Vector3(std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z));
            box.maxs = godot::Vector3(std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z));
            map_data.brush_models.push_back(std::move(box));

            model_ranges.emplace_back(
                models[i].first_face < 0 ? 0 : static_cast<size_t>(models[i].first_face),
                models[i].num_faces < 0 ? 0 : static_cast<size_t>(models[i].num_faces));
        }
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
    auto plane_lump = get_lump(planes_lump);
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

    // Lump 8: lighting, three bytes (R, G, B) per sample. A map compiled without light has
    // none, and then nothing below touches uv1 and the level stays evenly bright.
    const auto light_lump = get_lump(8);
    const bool lit_map = light_lump.size() >= 3;
    uint32_t atlas_width = 256;
    while (atlas_width < 4096 &&
           static_cast<size_t>(atlas_width) * atlas_width < light_lump.size() / 3 * 5 / 4) {
        atlas_width *= 2;
    }
    LightAtlas atlas(atlas_width);
    if (lit_map) atlas.place(kWhitePatch, kWhitePatch);   // lands at 0,0, already white

    // Group surfaces by texture index, per brush model.
    //
    // Per model, not per map, and that is the point. Lump 14 says which faces belong to which
    // brush: model 0 is the world and each of the rest is a door, a lift, a pool of water, a
    // decoration. Grouping every face together made all of them one object — so a door was
    // welded to every wall that shared its texture and could not move, and a func_illusionary
    // that the game walks straight through became a wall.
    //
    // A face outside every model's range is unreachable and does not get built. On a
    // well-formed map there are none.
    std::vector<std::pair<size_t, size_t>> ranges;   // first face, count
    for (const auto& model : model_ranges) ranges.push_back(model);
    if (ranges.empty()) ranges.emplace_back(0, num_faces);

    for (size_t model_index = 0; model_index < ranges.size(); ++model_index) {
    std::unordered_map<int32_t, ir::IRSurface> surface_map;

    const size_t face_from = ranges[model_index].first;
    const size_t face_to = std::min(face_from + ranges[model_index].second, num_faces);

    for (size_t f = face_from; f < face_to; ++f) {
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
                const std::string& n = miptex->name;
                if (n.size() >= 3 && std::tolower(static_cast<unsigned char>(n[0])) == 's' &&
                    std::tolower(static_cast<unsigned char>(n[1])) == 'k' &&
                    std::tolower(static_cast<unsigned char>(n[2])) == 'y' &&
                    (n.size() == 3 || n[3] == '_')) {
                    ++map_data.sky_faces;
                }
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
        std::vector<godot::Vector2> poly_st;   // texture-space position in texels, for light

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

                    // The same projection again for the light grid, and here the arithmetic
                    // is part of the format: the sum is taken in double precision and then
                    // rounded to a float, as the original engine's did. Skip the rounding
                    // and 4 faces in 100 come out one row or column too big, which shifts
                    // every grid stored after them. Measured on the 397 retail maps: with
                    // the rounding, 2,595,233 lit faces tile lump 8 with no gap or overlap.
                    const auto project = [&](const float axis[4]) {
                        const double sum = static_cast<double>(pos_raw.x) * axis[0] +
                                           static_cast<double>(pos_raw.y) * axis[1] +
                                           static_cast<double>(pos_raw.z) * axis[2] + axis[3];
                        return static_cast<float>(sum);
                    };
                    poly_st.emplace_back(project(tex_info->vector_s), project(tex_info->vector_t));
                } else {
                    poly_uvs.emplace_back(0.0f, 0.0f);
                    poly_st.emplace_back(0.0f, 0.0f);
                }
            }
        }

        // Where each vertex falls in the light atlas, in atlas pixels for now: the atlas is
        // still growing, so the division by its size waits until every face is placed.
        std::vector<godot::Vector2> poly_light(poly_positions.size(),
                                               godot::Vector2(kWhitePoint, kWhitePoint));
        if (lit_map && tex_info && face.lightmap_offset >= 0 && poly_positions.size() >= 3) {
            float min_s = std::numeric_limits<float>::max();
            float min_t = min_s;
            float max_s = std::numeric_limits<float>::lowest();
            float max_t = max_s;
            for (const auto& st : poly_st) {
                min_s = std::min(min_s, st.x);
                max_s = std::max(max_s, st.x);
                min_t = std::min(min_t, st.y);
                max_t = std::max(max_t, st.y);
            }
            // The grid runs from the sample at or before the face's first texel to the one
            // at or after its last, so a face 32 units wide has three samples across.
            const double first_s = std::floor(min_s / kLuxelUnits);
            const double first_t = std::floor(min_t / kLuxelUnits);
            const double across = std::ceil(max_s / kLuxelUnits) - first_s + 1.0;
            const double down = std::ceil(max_t / kLuxelUnits) - first_t + 1.0;

            // One grid per style in use; the list is filled from the front and 255 ends it.
            size_t style_count = 0;
            while (style_count < 4 && face.styles[style_count] != 255) ++style_count;

            if (style_count > 0 && across >= 1.0 && down >= 1.0 &&
                across <= kMaxLuxelsPerSide && down <= kMaxLuxelsPerSide) {
                const auto w = static_cast<uint32_t>(across);
                const auto h = static_cast<uint32_t>(down);
                const size_t grid_bytes = static_cast<size_t>(w) * h * 3;
                const auto offset = static_cast<size_t>(face.lightmap_offset);

                if (offset <= light_lump.size() &&
                    grid_bytes * style_count <= light_lump.size() - offset) {
                    const auto [rect_x, rect_y] = atlas.place(w, h);
                    const auto* samples =
                        reinterpret_cast<const uint8_t*>(light_lump.data()) + offset;
                    for (uint32_t y = 0; y < h; ++y) {
                        for (uint32_t x = 0; x < w; ++x) {
                            uint8_t* out = atlas.pixel(rect_x + x, rect_y + y);
                            for (int channel = 0; channel < 3; ++channel) {
                                unsigned sum = 0;
                                for (size_t style = 0; style < style_count; ++style) {
                                    // Its grid is there, but the lamp is off.
                                    if (style_starts_dark[face.styles[style]]) continue;
                                    sum += samples[style * grid_bytes +
                                                   (static_cast<size_t>(y) * w + x) * 3 + channel];
                                }
                                out[channel] = static_cast<uint8_t>(std::min(sum, 255u));
                            }
                        }
                    }
                    // Samples sit at the centre of their pixel, hence the half. That also
                    // keeps a smoothed lookup inside this face's own rectangle.
                    for (size_t i = 0; i < poly_st.size(); ++i) {
                        poly_light[i] = godot::Vector2(
                            static_cast<float>(rect_x + (poly_st[i].x / kLuxelUnits - first_s) + 0.5),
                            static_cast<float>(rect_y + (poly_st[i].y / kLuxelUnits - first_t) + 0.5));
                    }
                }
            }
        }

        // Fan triangulation for polygon faces
        if (poly_positions.size() >= 3) {
            for (size_t i = 0; i < poly_positions.size(); ++i) {
                surf.positions.push_back(poly_positions[i]);
                surf.normals.push_back(face_normal);
                surf.uv0.push_back(poly_uvs[i]);
                if (lit_map) surf.uv1.push_back(poly_light[i]);
            }

            for (size_t i = 1; i + 1 < poly_positions.size(); ++i) {
                surf.indices.push_back(base_index);
                surf.indices.push_back(base_index + static_cast<uint32_t>(i));
                surf.indices.push_back(base_index + static_cast<uint32_t>(i + 1));
            }
        }
    }

    // Model 0 is the world; the rest are entity brushes and go to whichever entity names them.
    ir::IRMeshData& into = model_index == 0
        ? map_data.mesh_data
        : map_data.brush_models[model_index].mesh;
    into.source_engine = ir::SourceEngine::GoldSrc;
    into.name = model_index == 0 ? "GoldSrcMap" : ("brush_" + std::to_string(model_index));

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
        into.surfaces.push_back(std::move(surf));
    }
    if (model_index != 0) {
        into.material_search_paths = map_data.mesh_data.material_search_paths;
    }
    }   // per brush model

    // Every face is placed, so the atlas has its final size and the pixel positions written
    // into uv1 can become the 0..1 coordinates a renderer wants.
    if (lit_map) {
        const float inv_w = 1.0f / static_cast<float>(atlas.width());
        const float inv_h = 1.0f / static_cast<float>(atlas.height());
        const auto normalise = [&](ir::IRMeshData& mesh) {
            for (auto& surf : mesh.surfaces) {
                for (auto& uv : surf.uv1) uv = godot::Vector2(uv.x * inv_w, uv.y * inv_h);
            }
        };
        normalise(map_data.mesh_data);
        for (auto& model : map_data.brush_models) normalise(model.mesh);

        map_data.lightmap.name = "lightmap";
        map_data.lightmap.width = atlas.width();
        map_data.lightmap.height = atlas.height();
        map_data.lightmap.rgba8_pixels = atlas.take_pixels();
    }

    // Collision: the solid parts of every model, cut out of the map's own tree (lump 5 nodes,
    // lump 10 leaves). See bsp30_solids.h for the method and for what it leaves out.
    //
    // Lump 9, the clipnodes, holds three more trees with the level fattened by the size of a
    // standing, large and crouching character, which is what the game's own movement code
    // walks a point through. That is the only place clip brushes exist. The standing one is
    // read too, into `player_solids`, kept apart because it is not the level at its real
    // size and an ordinary body must never meet it.
    map_data.collision_data.source_engine = ir::SourceEngine::GoldSrc;
    map_data.collision_data.name = "world";
    {
        const auto node_lump = get_lump(5);
        const auto leaf_lump = get_lump(10);
        const auto model_lump = get_lump(14);
        const std::span<const BSPNode> nodes(
            reinterpret_cast<const BSPNode*>(node_lump.data()), node_lump.size() / sizeof(BSPNode));
        const std::span<const BSPLeaf> leaves(
            reinterpret_cast<const BSPLeaf*>(leaf_lump.data()), leaf_lump.size() / sizeof(BSPLeaf));
        const std::span<const BSPPlane> planes(bsp_planes, num_planes);
        const auto* models = reinterpret_cast<const BSPModel*>(model_lump.data());
        const size_t model_count =
            std::min(model_lump.size() / sizeof(BSPModel), map_data.brush_models.size());
        const auto clip_lump = get_lump(9);
        const std::span<const BSPClipNode> clipnodes(
            reinterpret_cast<const BSPClipNode*>(clip_lump.data()),
            clip_lump.size() / sizeof(BSPClipNode));

        // The world's box ends at its outermost visible faces, and the solid behind those
        // lies outside it. Widened by this much (6.4 m), that solid survives as a thick shell
        // around the level rather than a skin. A brush entity is closed on every side by its own
        // planes and needs no more than the slack its box already has.
        constexpr float kWorldMargin = 256.0f;
        constexpr float kBrushMargin = 1.0f;
        // The player tree's solid reaches 16 units sideways and 36 up and down past the
        // model's own.
        constexpr float kPlayerReach = 40.0f;

        for (size_t i = 0; i < model_count && !nodes.empty() && !leaves.empty(); ++i) {
            map_data.brush_models[i].solids = build_model_solids(
                nodes, leaves, planes, models[i].head_nodes[0], models[i].mins, models[i].maxs,
                i == 0 ? kWorldMargin : kBrushMargin);
        }
        for (size_t i = 0; i < model_count && !clipnodes.empty(); ++i) {
            map_data.brush_models[i].player_solids = build_clip_solids(
                clipnodes, planes, models[i].head_nodes[1], models[i].mins, models[i].maxs,
                (i == 0 ? kWorldMargin : kBrushMargin) + kPlayerReach);
        }
        if (model_count > 0) {
            map_data.collision_data.convex_hulls = map_data.brush_models[0].solids;
        }
    }

    return map_data;
}

} // namespace quebratsk::parsers::goldsrc
