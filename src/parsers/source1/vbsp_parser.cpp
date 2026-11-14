#include "vbsp_parser.h"

#include "../../core/io/byte_reader.h"
#include "../../core/math/axis_remap.h"
#include "structs/vbsp_structs.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <map>
#include <type_traits>

namespace quebratsk::parsers::source1 {

namespace {

constexpr double kDegrees = 3.14159265358979323846 / 180.0;

/// A lump read as an array of its records. Every record type is packed, so reading one
/// from any byte offset is sound.
template <typename T>
std::span<const T> records(std::span<const std::byte> lump) {
    return {reinterpret_cast<const T*>(lump.data()), lump.size() / sizeof(T)};
}

godot::Vector3 raw(const float v[3]) { return godot::Vector3(v[0], v[1], v[2]); }

float project(const float axis[4], const godot::Vector3& p) {
    return p.x * axis[0] + p.y * axis[1] + p.z * axis[2] + axis[3];
}

/// The front of a triangle as Godot draws it, which is the side its corners run clockwise
/// on when looked at.
godot::Vector3 front_of(const godot::Vector3& a, const godot::Vector3& b, const godot::Vector3& c) {
    return (c - a).cross(b - a);
}

/// A stored colour and its shared power of two, as linear light.
godot::Color decode(const VbspLightSample& s) {
    const float gain = std::ldexp(1.0f, s.exponent) / 255.0f;
    return godot::Color(s.r * gain, s.g * gain, s.b * gain);
}

/// Which of the level's six cube faces each of Godot's is. The level's -Y is Godot's +X,
/// its +Z Godot's +Y and its -X Godot's +Z (see source_to_godot).
constexpr int kCubeFromLevel[6] = {3, 2, 4, 5, 1, 0};

void add_cube(VBSPAmbient& ambient, const VbspLightCube& cube) {
    for (const int face : kCubeFromLevel) ambient.colours.push_back(decode(cube.faces[face]));
}

/// A spot inside a leaf's box, given as fractions of it from 0 to 255.
godot::Vector3 spot_in(const int16_t mins[3], const int16_t maxs[3], uint8_t x, uint8_t y, uint8_t z) {
    const uint8_t f[3] = {x, y, z};
    godot::Vector3 p;
    for (int i = 0; i < 3; ++i) {
        p[i] = mins[i] + (maxs[i] - mins[i]) * (f[i] / 255.0f);
    }
    return math::source_to_godot(p);
}

/// The tree of planes and the light each leaf keeps.
template <typename Leaf>
VBSPAmbient read_ambient(std::span<const VbspNode> nodes, std::span<const VbspPlane> planes,
                         std::span<const Leaf> leaves, std::span<const VbspLeafAmbientIndex> index,
                         std::span<const VbspLeafAmbientSample> samples, int32_t root) {
    VBSPAmbient ambient;
    ambient.root = root;
    const auto k = static_cast<float>(math::kHammerUnitsToMeters);
    ambient.splits.reserve(nodes.size());
    for (const VbspNode& node : nodes) {
        VBSPAmbient::Split split;
        if (node.plane >= 0 && static_cast<size_t>(node.plane) < planes.size()) {
            const VbspPlane& plane = planes[static_cast<size_t>(node.plane)];
            // The same turn as source_to_godot, which keeps lengths, so only the distance
            // needs scaling.
            split.normal = godot::Vector3(-plane.normal[1], plane.normal[2], -plane.normal[0]);
            split.dist = plane.dist * k;
        }
        split.front = node.children[0];
        split.back = node.children[1];
        ambient.splits.push_back(split);
    }

    ambient.leaves.resize(leaves.size());
    for (size_t l = 0; l < leaves.size(); ++l) {
        const Leaf& leaf = leaves[l];
        VBSPAmbient::Range& range = ambient.leaves[l];
        range.first = static_cast<uint32_t>(ambient.spots.size());
        if (l < index.size()) {
            const VbspLeafAmbientIndex& at = index[l];
            for (uint32_t i = 0; i < at.sample_count; ++i) {
                const size_t which = static_cast<size_t>(at.first_sample) + i;
                if (which >= samples.size()) break;
                const VbspLeafAmbientSample& sample = samples[which];
                ambient.spots.push_back(spot_in(leaf.mins, leaf.maxs, sample.x, sample.y, sample.z));
                add_cube(ambient, sample.cube);
            }
        } else if constexpr (std::is_same_v<Leaf, VbspLeafV0>) {
            // The older leaves keep one cube each, for the middle of the leaf.
            if (index.empty()) {
                ambient.spots.push_back(spot_in(leaf.mins, leaf.maxs, 128, 128, 128));
                add_cube(ambient, leaf.ambient);
            }
        }
        range.count = static_cast<uint32_t>(ambient.spots.size()) - range.first;
    }
    return ambient;
}

/// One face's light, as a block of the light picture.
struct LightBlock {
    size_t face = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t x = 0;
    uint32_t y = 0;
};

constexpr uint32_t kWhiteBlock = 2;   // a patch of plain white, for faces drawn without light

/// Lay the blocks out on shelves, tallest first, one pixel apart so the smoothing of one
/// face's light never reaches into the next one's. Returns the picture's height.
uint32_t pack(std::vector<LightBlock>& blocks, uint32_t width) {
    std::vector<size_t> order(blocks.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(),
                     [&](size_t a, size_t b) { return blocks[a].height > blocks[b].height; });
    uint32_t x = 1;
    uint32_t y = 1;
    uint32_t shelf = 0;
    for (const size_t i : order) {
        LightBlock& block = blocks[i];
        if (x + block.width + 1 > width) {
            x = 1;
            y += shelf + 1;
            shelf = 0;
        }
        block.x = x;
        block.y = y;
        x += block.width + 1;
        shelf = std::max(shelf, block.height);
    }
    return y + shelf + 1;
}

/// A level's texture name, the way materials are looked up: lower case, forward slashes.
std::string texture_name(int32_t texdata, std::span<const VbspTexData> texdatas,
                         std::span<const int32_t> table, std::span<const std::byte> names) {
    if (texdata < 0 || static_cast<size_t>(texdata) >= texdatas.size()) return {};
    const int32_t id = texdatas[static_cast<size_t>(texdata)].name_string_table_id;
    if (id < 0 || static_cast<size_t>(id) >= table.size()) return {};
    const int32_t offset = table[static_cast<size_t>(id)];
    if (offset < 0 || static_cast<size_t>(offset) >= names.size()) return {};
    std::string out;
    for (size_t i = static_cast<size_t>(offset); i < names.size(); ++i) {
        const char c = static_cast<char>(names[i]);
        if (c == '\0') break;
        out.push_back(c == '\\' ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

} // namespace

bool VBSPParser::is_vbsp(std::span<const std::byte> bytes) {
    if (bytes.size() < sizeof(VbspHeader)) return false;
    int32_t ident = 0;
    std::memcpy(&ident, bytes.data(), sizeof(ident));
    return ident == kVbspMagic;
}

godot::Basis VBSPParser::angles_to_basis(float pitch, float yaw, float roll) {
    // The rotation in the level's own axes, columns forward, left and up, as the Source
    // SDK's mathlib AngleMatrix() builds it from a QAngle.
    const double p = pitch * kDegrees;
    const double y = yaw * kDegrees;
    const double r = roll * kDegrees;
    const double sp = std::sin(p), cp = std::cos(p);
    const double sy = std::sin(y), cy = std::cos(y);
    const double sr = std::sin(r), cr = std::cos(r);
    const double m[3][3] = {
        {cp * cy, sr * sp * cy - cr * sy, cr * sp * cy + sr * sy},
        {cp * sy, sr * sp * sy + cr * cy, cr * sp * sy - sr * cy},
        {-sp, sr * cp, cr * cp},
    };
    // The same rotation in Godot's axes: M R Mt, where M is source_to_godot(). M takes the
    // level's y, z and x (negating the first and last) to Godot's x, y and z.
    constexpr int from[3] = {1, 2, 0};
    constexpr double sign[3] = {-1.0, 1.0, -1.0};
    godot::Basis out;
    for (int a = 0; a < 3; ++a) {
        for (int b = 0; b < 3; ++b) {
            out[a][b] = static_cast<real_t>(sign[a] * sign[b] * m[from[a]][from[b]]);
        }
    }
    return out;
}

int32_t VBSPAmbient::leaf_at(const godot::Vector3& at) const {
    int32_t node = root;
    // A tree deeper than it has nodes can only be a loop in a broken file.
    for (size_t step = 0; step <= splits.size(); ++step) {
        if (node < 0) {
            const int32_t leaf = -(node + 1);
            return static_cast<size_t>(leaf) < leaves.size() ? leaf : -1;
        }
        if (static_cast<size_t>(node) >= splits.size()) return -1;
        const Split& split = splits[static_cast<size_t>(node)];
        node = split.normal.dot(at) - split.dist >= 0.0f ? split.front : split.back;
    }
    return -1;
}

bool VBSPAmbient::light_at(const godot::Vector3& at, godot::Color out[6]) const {
    const int32_t leaf = leaf_at(at);
    if (leaf < 0) return false;
    const Range& range = leaves[static_cast<size_t>(leaf)];
    if (range.count == 0) return false;
    for (int f = 0; f < 6; ++f) out[f] = godot::Color(0, 0, 0);
    // Nearer samples count for more, so walking across a leaf changes the light smoothly
    // rather than at the line halfway between two of them.
    float total = 0.0f;
    for (uint32_t i = range.first; i < range.first + range.count; ++i) {
        const float weight = 1.0f / (spots[i].distance_squared_to(at) + 0.01f);
        total += weight;
        for (int f = 0; f < 6; ++f) {
            const godot::Color& c = colours[static_cast<size_t>(i) * 6 + f];
            out[f].r += c.r * weight;
            out[f].g += c.g * weight;
            out[f].b += c.b * weight;
        }
    }
    for (int f = 0; f < 6; ++f) {
        out[f].r /= total;
        out[f].g /= total;
        out[f].b /= total;
        out[f].a = 1.0f;
    }
    return true;
}

std::expected<ParsedVBSPMap, VBSPParseError> VBSPParser::parse(std::span<const std::byte> bytes) {
    if (!is_vbsp(bytes)) return std::unexpected(VBSPParseError::InvalidHeader);
    const io::ByteReader file(bytes);
    VbspHeader header{};
    std::memcpy(&header, bytes.data(), sizeof(header));
    // 17 to 21 are the versions the Source SDK's own games write (Half-Life 2 to CS:GO and
    // Garry's Mod). Later ones rearrange the header.
    if (header.version < 17 || header.version > 21) {
        return std::unexpected(VBSPParseError::VersionMismatch);
    }

    ParsedVBSPMap map;
    map.version = header.version;
    map.revision = header.map_revision;

    const auto lump = [&](int32_t index) -> std::span<const std::byte> {
        const VbspLumpEntry& entry = header.lumps[index];
        if (entry.offset < 0 || entry.length <= 0) return {};
        const auto span = file.bytes_at(static_cast<size_t>(entry.offset),
                                        static_cast<size_t>(entry.length));
        // A compressed lump starts with "LZMA". Read as records it would be noise.
        if (span.size() >= 4 && std::memcmp(span.data(), "LZMA", 4) == 0) {
            ++map.stats.compressed_lumps;
            return {};
        }
        return span;
    };

    // ---- entities
    if (const auto text = lump(kLumpEntities); !text.empty()) {
        map.entity_string.assign(reinterpret_cast<const char*>(text.data()), text.size());
        while (!map.entity_string.empty() && map.entity_string.back() == '\0') {
            map.entity_string.pop_back();
        }
    }

    const auto planes = records<VbspPlane>(lump(kLumpPlanes));
    const auto texdatas = records<VbspTexData>(lump(kLumpTexData));
    const auto vertices = records<godot::Vector3>(lump(kLumpVertexes));
    const auto texinfos = records<VbspTexInfo>(lump(kLumpTexInfo));
    const auto edges = records<VbspEdge>(lump(kLumpEdges));
    const auto surfedges = records<int32_t>(lump(kLumpSurfEdges));
    const auto models = records<VbspModel>(lump(kLumpModels));
    const auto dispinfos = records<VbspDispInfo>(lump(kLumpDispInfo));
    const auto dispverts = records<VbspDispVert>(lump(kLumpDispVerts));
    const auto name_table = records<int32_t>(lump(kLumpTexDataStringTable));
    const auto name_data = lump(kLumpTexDataStringData);
    static_assert(sizeof(godot::Vector3) == 12, "vertices are read straight into Vector3");

    // The level is lit twice, once for each way the game can be run, and each copy of the
    // face list points into its own light. The HDR one is the fuller of the two and is
    // taken when it is there, with its own face list when that matches the plain one.
    auto faces_lump = lump(kLumpFaces);
    auto light = lump(kLumpLighting);
    if (const auto hdr = lump(kLumpLightingHdr); !hdr.empty()) {
        const auto hdr_faces = lump(kLumpFacesHdr);
        if (hdr_faces.size() == faces_lump.size()) {
            faces_lump = hdr_faces;
            light = hdr;
            map.hdr_light = true;
        } else if (light.empty()) {
            light = hdr;
            map.hdr_light = true;
        }
    }
    const auto faces = records<VbspFace>(faces_lump);
    map.stats.faces_in_file = faces.size();

    const auto corner_of = [&](int32_t surfedge_index) -> int32_t {
        if (surfedge_index < 0 || static_cast<size_t>(surfedge_index) >= surfedges.size()) return -1;
        const int32_t surfedge = surfedges[static_cast<size_t>(surfedge_index)];
        const size_t edge = surfedge >= 0 ? static_cast<size_t>(surfedge)
                                          : static_cast<size_t>(0u - static_cast<uint32_t>(surfedge));
        if (edge >= edges.size()) return -1;
        const uint16_t v = edges[edge].v[surfedge >= 0 ? 0 : 1];
        return v < vertices.size() ? static_cast<int32_t>(v) : -1;
    };

    const auto drawn_texinfo = [&](const VbspFace& face) -> const VbspTexInfo* {
        if (face.texinfo < 0 || static_cast<size_t>(face.texinfo) >= texinfos.size()) return nullptr;
        return &texinfos[static_cast<size_t>(face.texinfo)];
    };
    constexpr int32_t kNotDrawn =
        kSurfSky | kSurfSky2D | kSurfNoDraw | kSurfHint | kSurfSkip | kSurfTrigger;

    // ---- which faces each model owns
    std::vector<std::pair<size_t, size_t>> ranges;
    for (const auto& model : models) {
        const size_t first = model.first_face < 0 ? 0 : static_cast<size_t>(model.first_face);
        const size_t count = model.num_faces < 0 ? 0 : static_cast<size_t>(model.num_faces);
        ranges.emplace_back(std::min(first, faces.size()),
                            std::min(first + count, faces.size()));
    }
    if (ranges.empty()) ranges.emplace_back(0, faces.size());

    // ---- the light picture: one block per lit face, laid out before any geometry is built
    std::vector<LightBlock> blocks;
    blocks.push_back({SIZE_MAX, kWhiteBlock, kWhiteBlock});
    std::vector<int32_t> block_of(faces.size(), -1);
    uint64_t area = 0;
    uint32_t widest = kWhiteBlock;
    for (const auto& [from, to] : ranges) {
        for (size_t f = from; f < to; ++f) {
            const VbspFace& face = faces[f];
            const VbspTexInfo* info = drawn_texinfo(face);
            if (info == nullptr || (info->flags & kNotDrawn) != 0) continue;
            if (face.light_offset < 0 || face.styles[0] == 255 || light.empty()) continue;
            const int64_t w = static_cast<int64_t>(face.lightmap_size[0]) + 1;
            const int64_t h = static_cast<int64_t>(face.lightmap_size[1]) + 1;
            if (w < 1 || h < 1 || w > 1024 || h > 1024) continue;
            if (static_cast<uint64_t>(face.light_offset) + static_cast<uint64_t>(w * h * 4) >
                light.size()) {
                continue;
            }
            block_of[f] = static_cast<int32_t>(blocks.size());
            blocks.push_back({f, static_cast<uint32_t>(w), static_cast<uint32_t>(h)});
            area += static_cast<uint64_t>(w + 1) * static_cast<uint64_t>(h + 1);
            widest = std::max(widest, static_cast<uint32_t>(w));
        }
    }
    uint32_t sheet_width = 256;
    while (sheet_width < 8192 &&
           (static_cast<uint64_t>(sheet_width) * sheet_width < area * 5 / 4 ||
            sheet_width < widest + 2)) {
        sheet_width *= 2;
    }
    const uint32_t sheet_height = pack(blocks, sheet_width);
    map.lightmap.width = sheet_width;
    map.lightmap.height = sheet_height;
    map.lightmap.rgb.assign(static_cast<size_t>(sheet_width) * sheet_height * 3, 1.0f);
    for (size_t b = 1; b < blocks.size(); ++b) {
        const LightBlock& block = blocks[b];
        const VbspFace& face = faces[block.face];
        // The first block of style 0. A face lit for bump mapping keeps three more after
        // it, one per direction of the bumps; the first is the light without them.
        const auto* samples = reinterpret_cast<const uint8_t*>(light.data()) + face.light_offset;
        for (uint32_t y = 0; y < block.height; ++y) {
            for (uint32_t x = 0; x < block.width; ++x) {
                const uint8_t* s = samples + (static_cast<size_t>(y) * block.width + x) * 4;
                // Colour and a shared power of two: linear light, which can pass 1.
                const float gain = std::ldexp(1.0f, static_cast<int8_t>(s[3])) / 255.0f;
                float* out = map.lightmap.rgb.data() +
                             ((static_cast<size_t>(block.y) + y) * sheet_width + block.x + x) * 3;
                out[0] = s[0] * gain;
                out[1] = s[1] * gain;
                out[2] = s[2] * gain;
            }
        }
    }
    const godot::Vector2 sheet_size(static_cast<float>(sheet_width), static_cast<float>(sheet_height));
    const godot::Vector2 white_point =
        godot::Vector2(blocks[0].x + 1.0f, blocks[0].y + 1.0f) / sheet_size;
    map.stats.lit_faces = blocks.size() - 1;

    // ---- the faces, one mesh per model, one surface per texture
    std::map<std::string, bool> textures_seen;
    for (size_t m = 0; m < ranges.size(); ++m) {
        VBSPBrushModel model;
        if (m < models.size()) {
            const godot::Vector3 a = math::source_to_godot(raw(models[m].mins));
            const godot::Vector3 b = math::source_to_godot(raw(models[m].maxs));
            model.mins = godot::Vector3(std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z));
            model.maxs = godot::Vector3(std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z));
        }
        model.mesh.source_engine = ir::SourceEngine::Source1;
        std::map<std::string, size_t> surface_of;

        for (size_t f = ranges[m].first; f < ranges[m].second; ++f) {
            const VbspFace& face = faces[f];
            const VbspTexInfo* info = drawn_texinfo(face);
            if (info == nullptr) continue;
            if ((info->flags & kNotDrawn) != 0) {
                ++map.stats.faces_not_drawn;
                if ((info->flags & (kSurfSky | kSurfSky2D)) != 0) ++map.sky_faces;
                continue;
            }

            const std::string name = texture_name(info->texdata, texdatas, name_table, name_data);
            auto [slot, added] = surface_of.try_emplace(name, model.mesh.surfaces.size());
            if (added) {
                model.mesh.surfaces.emplace_back();
                model.mesh.surfaces.back().material_name = name;
                textures_seen[name] = true;
            }
            ir::IRSurface& surf = model.mesh.surfaces[slot->second];

            float tex_w = 64.0f;
            float tex_h = 64.0f;
            if (info->texdata >= 0 && static_cast<size_t>(info->texdata) < texdatas.size()) {
                tex_w = static_cast<float>(std::max(texdatas[static_cast<size_t>(info->texdata)].width, 1));
                tex_h = static_cast<float>(std::max(texdatas[static_cast<size_t>(info->texdata)].height, 1));
            }
            if ((info->flags & kSurfWarp) != 0) ++map.stats.water_faces;

            const int32_t block_index = block_of[f];
            const LightBlock* block = block_index >= 0 ? &blocks[static_cast<size_t>(block_index)] : nullptr;
            if (block == nullptr) ++map.stats.unlit_faces;

            // A face's plane already faces the way the face does: its corners run clockwise
            // seen from the side the normal points to, whatever `side` says (that flag is
            // about the node the face was cut from, not about the face).
            godot::Vector3 normal(0, 1, 0);
            if (face.plane < planes.size()) {
                normal = math::transform_normal_zup_to_yup(raw(planes[face.plane].normal));
            }

            // Where a point of the level falls on the texture and on the light picture.
            const auto texture_uv = [&](const godot::Vector3& p) {
                return godot::Vector2(project(info->texture_vecs[0], p) / tex_w,
                                      project(info->texture_vecs[1], p) / tex_h);
            };
            const auto light_uv = [&](const godot::Vector3& p) {
                if (block == nullptr) return white_point;
                const float u = project(info->lightmap_vecs[0], p) - face.lightmap_mins[0] + 0.5f;
                const float v = project(info->lightmap_vecs[1], p) - face.lightmap_mins[1] + 0.5f;
                return godot::Vector2(block->x + u, block->y + v) / sheet_size;
            };

            std::vector<godot::Vector3> ring;
            ring.reserve(static_cast<size_t>(std::max<int16_t>(face.num_edges, 0)));
            for (int16_t e = 0; e < face.num_edges; ++e) {
                const int32_t v = corner_of(face.first_edge + e);
                if (v >= 0) ring.push_back(vertices[static_cast<size_t>(v)]);
            }

            if (face.dispinfo < 0) {
                if (ring.size() < 3) continue;
                const auto base = static_cast<uint32_t>(surf.positions.size());
                for (const auto& p : ring) {
                    surf.positions.push_back(math::source_to_godot(p));
                    surf.normals.push_back(normal);
                    surf.uv0.push_back(texture_uv(p));
                    surf.uv1.push_back(light_uv(p));
                }
                for (uint32_t i = 1; i + 1 < ring.size(); ++i) {
                    surf.indices.push_back(base);
                    surf.indices.push_back(base + i);
                    surf.indices.push_back(base + i + 1);
                }
                ++map.stats.faces_drawn;
                continue;
            }

            // ---- a displacement: the face is the frame, and a grid of points is pushed off it
            ++map.stats.displacements;
            if (ring.size() != 4 || static_cast<size_t>(face.dispinfo) >= dispinfos.size()) {
                ++map.stats.displacements_bad;
                continue;
            }
            const VbspDispInfo& disp = dispinfos[static_cast<size_t>(face.dispinfo)];
            if (disp.power < 1 || disp.power > 4 || disp.disp_vert_start < 0) {
                ++map.stats.displacements_bad;
                continue;
            }
            const int side = (1 << disp.power) + 1;
            const size_t count = static_cast<size_t>(side) * side;
            if (static_cast<size_t>(disp.disp_vert_start) + count > dispverts.size()) {
                ++map.stats.displacements_bad;
                continue;
            }
            // The grid starts at the corner the displacement names, which need not be the
            // face's first.
            const godot::Vector3 start = raw(disp.start_position);
            size_t first = 0;
            for (size_t i = 1; i < 4; ++i) {
                if (ring[i].distance_squared_to(start) < ring[first].distance_squared_to(start)) first = i;
            }
            const godot::Vector3 c0 = ring[first];
            const godot::Vector3 c1 = ring[(first + 1) % 4];
            const godot::Vector3 c2 = ring[(first + 2) % 4];
            const godot::Vector3 c3 = ring[(first + 3) % 4];

            // A displacement's light is laid over its grid, not projected on its frame: the
            // block's corners are the grid's corners (the frame can be bigger or smaller than
            // the block, since the light was sized to the bumped surface). Which way the
            // block's two axes run along the grid comes from projecting the frame's edges.
            const godot::Vector2 p0(project(info->lightmap_vecs[0], c0), project(info->lightmap_vecs[1], c0));
            const godot::Vector2 p1(project(info->lightmap_vecs[0], c1), project(info->lightmap_vecs[1], c1));
            const godot::Vector2 p3(project(info->lightmap_vecs[0], c3), project(info->lightmap_vecs[1], c3));
            const godot::Vector2 along_rows = p1 - p0;   // the way `row` grows
            const godot::Vector2 along_cols = p3 - p0;   // the way `col` grows
            const bool rows_run_along_s = std::fabs(along_rows.x) >= std::fabs(along_rows.y);
            const auto grid_light_uv = [&](float row_t, float col_t) {
                if (block == nullptr) return white_point;
                float s_t = rows_run_along_s ? row_t : col_t;
                float t_t = rows_run_along_s ? col_t : row_t;
                if ((rows_run_along_s ? along_rows.x : along_cols.x) < 0.0f) s_t = 1.0f - s_t;
                if ((rows_run_along_s ? along_cols.y : along_rows.y) < 0.0f) t_t = 1.0f - t_t;
                const float u = s_t * static_cast<float>(face.lightmap_size[0]) + 0.5f;
                const float v = t_t * static_cast<float>(face.lightmap_size[1]) + 0.5f;
                return godot::Vector2(block->x + u, block->y + v) / sheet_size;
            };

            const auto base_index = static_cast<uint32_t>(surf.positions.size());
            std::vector<godot::Vector3> flat(count);
            for (int row = 0; row < side; ++row) {
                const float along = static_cast<float>(row) / static_cast<float>(side - 1);
                const godot::Vector3 from = c0.lerp(c1, along);
                const godot::Vector3 to = c3.lerp(c2, along);
                for (int col = 0; col < side; ++col) {
                    const float across = static_cast<float>(col) / static_cast<float>(side - 1);
                    const size_t k = static_cast<size_t>(row) * side + col;
                    flat[k] = from.lerp(to, across);
                    const VbspDispVert& dv = dispverts[static_cast<size_t>(disp.disp_vert_start) + k];
                    const godot::Vector3 moved = flat[k] + raw(dv.vec) * dv.dist;
                    surf.positions.push_back(math::source_to_godot(moved));
                    // The texture is spread over the frame, not over the bumps.
                    surf.uv0.push_back(texture_uv(flat[k]));
                    surf.uv1.push_back(grid_light_uv(along, across));
                    surf.normals.push_back(godot::Vector3());
                }
            }
            // Two triangles a square, the diagonal alternating like a chequerboard, and
            // turned to face the way the frame does.
            std::vector<uint32_t> tris;
            tris.reserve(static_cast<size_t>(side - 1) * (side - 1) * 6);
            for (int row = 0; row + 1 < side; ++row) {
                for (int col = 0; col + 1 < side; ++col) {
                    const uint32_t i0 = static_cast<uint32_t>(row * side + col);
                    const uint32_t i1 = i0 + 1;
                    const uint32_t i2 = i0 + static_cast<uint32_t>(side);
                    const uint32_t i3 = i2 + 1;
                    if ((row + col) % 2 == 0) {
                        tris.insert(tris.end(), {i0, i2, i3, i0, i3, i1});
                    } else {
                        tris.insert(tris.end(), {i0, i2, i1, i1, i2, i3});
                    }
                }
            }
            const godot::Vector3 frame_front =
                front_of(math::source_to_godot(flat[tris[0]]), math::source_to_godot(flat[tris[1]]),
                         math::source_to_godot(flat[tris[2]]));
            if (frame_front.dot(normal) < 0.0f) {
                for (size_t t = 0; t < tris.size(); t += 3) std::swap(tris[t + 1], tris[t + 2]);
            }
            for (size_t t = 0; t < tris.size(); t += 3) {
                const uint32_t a = base_index + tris[t];
                const uint32_t b = base_index + tris[t + 1];
                const uint32_t c = base_index + tris[t + 2];
                const godot::Vector3 n =
                    front_of(surf.positions[a], surf.positions[b], surf.positions[c]);
                surf.normals[a] += n;
                surf.normals[b] += n;
                surf.normals[c] += n;
                surf.indices.insert(surf.indices.end(), {a, b, c});
            }
            for (size_t k = 0; k < count; ++k) {
                godot::Vector3& n = surf.normals[base_index + k];
                n = n.length_squared() > 0.0f ? n.normalized() : normal;
            }
            ++map.stats.faces_drawn;
        }
        map.brush_models.push_back(std::move(model));
    }
    map.stats.textures = textures_seen.size();

    // ---- static props, from the game lump
    if (const auto game = lump(kLumpGame); game.size() >= 4) {
        const io::ByteReader directory(game);
        const int32_t count = directory.read_at<int32_t>(0).value_or(0);
        const auto entries = directory.array_at<VbspGameLump>(4, count > 0 ? static_cast<size_t>(count) : 0);
        for (const VbspGameLump& entry : entries) {
            if (entry.id != kGameLumpStaticProps || entry.offset < 0 || entry.length <= 0) continue;
            const io::ByteReader sprp(file.bytes_at(static_cast<size_t>(entry.offset),
                                                    static_cast<size_t>(entry.length)));
            if (sprp.empty()) break;
            map.prop_lump_version = entry.version;

            size_t at = 0;
            const int32_t names = sprp.read_at<int32_t>(at).value_or(-1);
            if (names < 0) break;
            at += 4;
            for (int32_t i = 0; i < names; ++i) {
                const auto raw_name = sprp.bytes_at(at, 128);
                if (raw_name.empty()) break;
                std::string name;
                for (const std::byte b : raw_name) {
                    const char c = static_cast<char>(b);
                    if (c == '\0') break;
                    name.push_back(c == '\\' ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
                }
                map.prop_models.push_back(std::move(name));
                at += 128;
            }
            const int32_t leaves = sprp.read_at<int32_t>(at).value_or(-1);
            if (leaves < 0) break;
            at += 4 + static_cast<size_t>(leaves) * 2;
            const int32_t props = sprp.read_at<int32_t>(at).value_or(0);
            at += 4;
            if (props <= 0 || at > sprp.size()) break;
            // Each version adds fields at the end of the record, and the first ones stay put.
            // The record's size is whatever is left, shared out.
            const size_t stride = (sprp.size() - at) / static_cast<size_t>(props);
            if (stride < 36) break;
            for (int32_t i = 0; i < props; ++i) {
                const size_t p = at + static_cast<size_t>(i) * stride;
                float f[6] = {};
                for (int k = 0; k < 6; ++k) f[k] = sprp.read_at<float>(p + k * 4).value_or(0.0f);
                const uint16_t which = sprp.read_at<uint16_t>(p + 24).value_or(0);
                VBSPStaticProp prop;
                prop.model = which < map.prop_models.size() ? map.prop_models[which] : std::string();
                prop.solid = sprp.read_at<uint8_t>(p + 30).value_or(0) != 0;
                prop.skin = sprp.read_at<int32_t>(p + 32).value_or(0);
                prop.transform = godot::Transform3D(angles_to_basis(f[3], f[4], f[5]),
                                                    math::source_to_godot(godot::Vector3(f[0], f[1], f[2])));
                map.props.push_back(std::move(prop));
            }
            break;
        }
    }

    // ---- the light kept for things that move, HDR first as for the faces
    {
        const auto nodes = records<VbspNode>(lump(kLumpNodes));
        auto index = records<VbspLeafAmbientIndex>(lump(kLumpLeafAmbientIndexHdr));
        auto samples = records<VbspLeafAmbientSample>(lump(kLumpLeafAmbientLightingHdr));
        bool hdr = !index.empty() && !samples.empty();
        if (!hdr) {
            index = records<VbspLeafAmbientIndex>(lump(kLumpLeafAmbientIndex));
            samples = records<VbspLeafAmbientSample>(lump(kLumpLeafAmbientLighting));
        }
        const int32_t root = models.empty() ? 0 : models[0].head_node;
        const auto leaf_lump = lump(kLumpLeafs);
        if (header.lumps[kLumpLeafs].version == 0) {
            map.ambient = read_ambient(nodes, planes, records<VbspLeafV0>(leaf_lump), index,
                                       samples, root);
        } else {
            map.ambient = read_ambient(nodes, planes, records<VbspLeaf>(leaf_lump), index,
                                       samples, root);
        }
        map.ambient.hdr = hdr;
        map.stats.light_samples = map.ambient.spots.size();
    }

    map.pakfile = lump(kLumpPakfile);
    return map;
}

} // namespace quebratsk::parsers::source1
