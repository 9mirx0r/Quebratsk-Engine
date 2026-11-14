// Verification of the Source level reader.
//
// The first half builds a tiny level by hand: one lit face, one displacement with two bumps
// in it, one sky face, one prop and a world entity. Each comes back where the format says it
// should: the face's light in the picture with the right values and coordinates, the
// displacement's grid turned to the corner it names, the sky face left out and counted, the
// prop standing at its origin facing its yaw.
//
// The second half, given a level or a folder of them, reads real files and checks what can
// be checked without a picture of the game:
//   - every face is drawn or accounted for;
//   - flat faces face the way their planes do once in Godot's axes;
//   - a displacement's edge meets its neighbours' edges, which is what decides which way its
//     grid runs (the two readings are compared, and the one the file agrees with wins);
//   - a face's corners fall inside its own block of light.
//
// No framework: prints its checks and exits non-zero on failure.

#include "parsers/source1/vbsp_parser.h"

#include "core/math/axis_remap.h"
#include "parsers/source1/structs/vbsp_structs.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <span>
#include <string>
#include <vector>

using namespace quebratsk::parsers::source1;

static int failures = 0;

static void check(const char* what, bool ok) {
    if (!ok) {
        std::printf("  FAIL %s\n", what);
        ++failures;
    }
}

static bool near(float a, float b, float tolerance = 1e-4f) { return std::fabs(a - b) <= tolerance; }
static bool near(const godot::Vector3& a, const godot::Vector3& b, float tolerance = 1e-4f) {
    return near(a.x, b.x, tolerance) && near(a.y, b.y, tolerance) && near(a.z, b.z, tolerance);
}

template <typename T>
static void put(std::vector<uint8_t>& out, const T& value) {
    const auto* p = reinterpret_cast<const uint8_t*>(&value);
    out.insert(out.end(), p, p + sizeof(T));
}

static std::span<const std::byte> as_bytes(const std::vector<uint8_t>& v) {
    return {reinterpret_cast<const std::byte*>(v.data()), v.size()};
}

// ------------------------------------------------------------ the hand-built level ----

static std::vector<uint8_t> build_level() {
    std::vector<uint8_t> file(sizeof(VbspHeader), 0);
    VbspHeader header{};
    header.ident = kVbspMagic;
    header.version = 20;
    header.map_revision = 7;
    auto add = [&](int index, const std::vector<uint8_t>& data) {
        while (file.size() % 4 != 0) file.push_back(0);
        header.lumps[index].offset = static_cast<int32_t>(file.size());
        header.lumps[index].length = static_cast<int32_t>(data.size());
        file.insert(file.end(), data.begin(), data.end());
    };

    const std::string entities =
        "{\n\"classname\" \"worldspawn\"\n\"skyname\" \"sky_day01_01\"\n}\n";
    add(kLumpEntities, std::vector<uint8_t>(entities.begin(), entities.end()));

    std::vector<uint8_t> planes;
    put(planes, VbspPlane{{0, 0, 1}, 0, 2});
    add(kLumpPlanes, planes);

    // Texture names: "Concrete\Floor01" and "TOOLS/TOOLSSKYBOX".
    const std::string names = std::string("Concrete\\Floor01") + '\0' + "TOOLS/TOOLSSKYBOX" + '\0';
    add(kLumpTexDataStringData, std::vector<uint8_t>(names.begin(), names.end()));
    std::vector<uint8_t> table;
    put(table, int32_t{0});
    put(table, int32_t{17});
    add(kLumpTexDataStringTable, table);
    std::vector<uint8_t> texdata;
    put(texdata, VbspTexData{{0.5f, 0.5f, 0.5f}, 0, 64, 32, 64, 32});
    put(texdata, VbspTexData{{0.5f, 0.5f, 0.5f}, 1, 32, 32, 32, 32});
    add(kLumpTexData, texdata);

    // Vertices: a 32 by 16 floor at z 0 (0-3), a 64 square at z 100 (4-7), a sky face (8-10).
    const float verts[][3] = {
        {0, 0, 0}, {0, 16, 0}, {32, 16, 0}, {32, 0, 0},
        {64, 0, 100}, {0, 0, 100}, {0, 64, 100}, {64, 64, 100},
        {0, 0, 500}, {0, 8, 500}, {8, 0, 500},
    };
    std::vector<uint8_t> vertex_lump;
    for (const auto& v : verts) {
        put(vertex_lump, v);
    }
    add(kLumpVertexes, vertex_lump);

    // Edges and surfedges: each face's corners in order, one edge per corner.
    std::vector<uint8_t> edges;
    std::vector<uint8_t> surfedges;
    put(edges, VbspEdge{{0, 0}});   // edge 0 unused, as in real files
    const int rings[][4] = {{0, 1, 2, 3}, {4, 5, 6, 7}, {8, 9, 10, -1}};
    int32_t edge_count = 1;
    for (const auto& ring : rings) {
        const int n = ring[3] < 0 ? 3 : 4;
        for (int i = 0; i < n; ++i) {
            put(edges, VbspEdge{{static_cast<uint16_t>(ring[i]), static_cast<uint16_t>(ring[(i + 1) % n])}});
            // Every other edge read backwards, as a level stores edges shared by two faces.
            if (i % 2 == 0) {
                put(surfedges, edge_count);
            } else {
                // Same edge stored the other way round.
                std::memcpy(edges.data() + edges.size() - 4,
                            std::array<uint16_t, 2>{static_cast<uint16_t>(ring[(i + 1) % n]),
                                                    static_cast<uint16_t>(ring[i])}.data(), 4);
                put(surfedges, -edge_count);
            }
            ++edge_count;
        }
    }
    add(kLumpEdges, edges);
    add(kLumpSurfEdges, surfedges);

    std::vector<uint8_t> texinfo;
    VbspTexInfo floor{};
    floor.texture_vecs[0][0] = 1.0f;
    floor.texture_vecs[1][1] = 1.0f;
    floor.lightmap_vecs[0][0] = 1.0f / 16.0f;
    floor.lightmap_vecs[1][1] = 1.0f / 16.0f;
    floor.flags = 0;
    floor.texdata = 0;
    put(texinfo, floor);
    VbspTexInfo sky = floor;
    sky.flags = kSurfSky | kSurfNoDraw;
    sky.texdata = 1;
    put(texinfo, sky);
    add(kLumpTexInfo, texinfo);

    std::vector<uint8_t> faces;
    VbspFace lit{};
    lit.plane = 0;
    lit.first_edge = 0;
    lit.num_edges = 4;
    lit.texinfo = 0;
    lit.dispinfo = -1;
    lit.styles[0] = 0;
    lit.styles[1] = lit.styles[2] = lit.styles[3] = 255;
    lit.light_offset = 0;
    lit.lightmap_mins[0] = 0;
    lit.lightmap_mins[1] = 0;
    lit.lightmap_size[0] = 2;   // 3 luxels across
    lit.lightmap_size[1] = 1;   // 2 down
    put(faces, lit);
    VbspFace bumpy = lit;
    bumpy.first_edge = 4;
    bumpy.dispinfo = 0;
    // Its light is 2 by 2 samples, though its frame projects to 4 luxels a side: the light
    // of a displacement is sized to its bumps and laid over its grid.
    bumpy.light_offset = 6 * 4;
    bumpy.lightmap_size[0] = 1;
    bumpy.lightmap_size[1] = 1;
    put(faces, bumpy);
    VbspFace skyface = lit;
    skyface.first_edge = 8;
    skyface.num_edges = 3;
    skyface.texinfo = 1;
    skyface.light_offset = -1;
    put(faces, skyface);
    add(kLumpFaces, faces);

    // Light: the floor's 3 by 2 samples, (255,128,0) at exponent 0, then (100,100,100) at
    // exponent 1; then the displacement's 2 by 2, (50,60,70) at exponent -1.
    std::vector<uint8_t> light;
    for (int i = 0; i < 6; ++i) {
        if (i == 0) {
            light.insert(light.end(), {255, 128, 0, 0});
        } else {
            light.insert(light.end(), {100, 100, 100, 1});
        }
    }
    for (int i = 0; i < 4; ++i) {
        light.insert(light.end(), {50, 60, 70, 0xFF});
    }
    add(kLumpLighting, light);

    std::vector<uint8_t> models;
    put(models, VbspModel{{0, 0, 0}, {64, 64, 500}, {0, 0, 0}, 0, 0, 3});
    add(kLumpModels, models);

    // The displacement: power 2, a 5 by 5 grid starting at (0,0,100), which is the face's
    // second corner. Two bumps: the middle raised 10, the point one step along the first
    // row raised 5.
    std::vector<uint8_t> dispinfo;
    VbspDispInfo disp{};
    disp.start_position[0] = 0;
    disp.start_position[1] = 0;
    disp.start_position[2] = 100;
    disp.disp_vert_start = 0;
    disp.power = 2;
    disp.map_face = 1;
    put(dispinfo, disp);
    add(kLumpDispInfo, dispinfo);
    std::vector<uint8_t> dispverts;
    for (int k = 0; k < 25; ++k) {
        const float dist = k == 12 ? 10.0f : (k == 1 ? 5.0f : 0.0f);
        put(dispverts, VbspDispVert{{0, 0, 1}, dist, 0});
    }
    add(kLumpDispVerts, dispverts);

    // The game lump: a directory of one, then the static props.
    std::vector<uint8_t> sprp;
    put(sprp, int32_t{1});
    char name[128] = {};
    std::strcpy(name, "models\\Props\\Crate.mdl");
    sprp.insert(sprp.end(), name, name + 128);
    put(sprp, int32_t{2});   // two leaves
    put(sprp, uint16_t{3});
    put(sprp, uint16_t{4});
    put(sprp, int32_t{1});   // one prop
    std::vector<uint8_t> record(64, 0);
    const float place[6] = {100, 200, 300, 0, 90, 0};
    std::memcpy(record.data(), place, sizeof(place));
    const uint16_t model_index = 0;
    std::memcpy(record.data() + 24, &model_index, 2);
    record[30] = 6;   // SOLID_VPHYSICS
    const int32_t skin = 2;
    std::memcpy(record.data() + 32, &skin, 4);
    sprp.insert(sprp.end(), record.begin(), record.end());

    while (file.size() % 4 != 0) file.push_back(0);
    std::vector<uint8_t> game;
    put(game, int32_t{1});
    const int32_t sprp_at = static_cast<int32_t>(file.size() + 4 + sizeof(VbspGameLump));
    put(game, VbspGameLump{kGameLumpStaticProps, 0, 6, sprp_at, static_cast<int32_t>(sprp.size())});
    game.insert(game.end(), sprp.begin(), sprp.end());
    add(kLumpGame, game);

    std::memcpy(file.data(), &header, sizeof(header));
    return file;
}

static void test_hand_built() {
    std::printf("hand-built level\n");
    const std::vector<uint8_t> file = build_level();
    const auto parsed = VBSPParser::parse(as_bytes(file));
    check("parses", parsed.has_value());
    if (!parsed.has_value()) return;
    const ParsedVBSPMap& map = *parsed;

    check("version", map.version == 20);
    check("revision", map.revision == 7);
    check("entities", map.entity_string.find("sky_day01_01") != std::string::npos);
    check("one model", map.brush_models.size() == 1);
    check("sky face counted", map.sky_faces == 1);
    check("faces drawn", map.stats.faces_drawn == 2);
    check("faces not drawn", map.stats.faces_not_drawn == 1);
    check("one displacement", map.stats.displacements == 1 && map.stats.displacements_bad == 0);
    check("both lit", map.stats.lit_faces == 2 && map.stats.unlit_faces == 0);

    const auto& mesh = map.brush_models[0].mesh;
    check("one surface for one texture", mesh.surfaces.size() == 1);
    if (mesh.surfaces.empty()) return;
    const auto& surf = mesh.surfaces[0];
    check("texture name lower case, forward slashes", surf.material_name == "concrete/floor01");
    check("4 + 25 points", surf.positions.size() == 29);
    check("2 + 32 triangles", surf.indices.size() == (2 + 32) * 3);
    check("arrays agree", surf.normals.size() == 29 && surf.uv0.size() == 29 && surf.uv1.size() == 29);
    if (surf.positions.size() != 29 || surf.uv1.size() != 29) return;

    constexpr float k = 0.0254f;
    // The floor: corner (32,16,0) on the texture at (32/64, 16/32).
    check("floor corner position", near(surf.positions[2], godot::Vector3(-16 * k, 0, -32 * k)));
    check("floor corner uv", near(surf.uv0[2].x, 0.5f) && near(surf.uv0[2].y, 0.5f));
    check("floor normal is up", near(surf.normals[0], godot::Vector3(0, 1, 0)));
    // Its triangles face up in Godot's axes (clockwise seen from above is the front).
    {
        const auto& a = surf.positions[surf.indices[0]];
        const auto& b = surf.positions[surf.indices[1]];
        const auto& c = surf.positions[surf.indices[2]];
        check("floor triangle faces up", (c - a).cross(b - a).y > 0.0f);
    }

    // The light: its block of 3 by 2 somewhere in the picture, the face's first corner on
    // the middle of the first sample.
    const auto& lm = map.lightmap;
    check("light picture made", lm.is_valid());
    if (!lm.is_valid()) return;
    const godot::Vector2 first = surf.uv1[0] * godot::Vector2(lm.width, lm.height);
    const int px = static_cast<int>(std::floor(first.x));
    const int py = static_cast<int>(std::floor(first.y));
    check("first corner on a sample's middle", near(first.x - px, 0.5f) && near(first.y - py, 0.5f));
    const auto sample = [&](int x, int y, int c) {
        return lm.rgb[(static_cast<size_t>(y) * lm.width + x) * 3 + c];
    };
    check("first sample is exponent 0", near(sample(px, py, 0), 1.0f) &&
                                             near(sample(px, py, 1), 128.0f / 255.0f) &&
                                             near(sample(px, py, 2), 0.0f));
    check("second sample doubled", near(sample(px + 1, py, 0), 200.0f / 255.0f));
    check("last sample", near(sample(px + 2, py + 1, 2), 200.0f / 255.0f));
    const godot::Vector2 far_corner = surf.uv1[2] * godot::Vector2(lm.width, lm.height);
    check("far corner on the last sample", near(far_corner.x, px + 2.5f) && near(far_corner.y, py + 1.5f));

    // The displacement: its grid starts at the corner it names, (0,0,100). Its rows run
    // towards the face's next corner, (0,64,100), so a row's points step towards the corner
    // before it, (64,0,100). Both bumps where they belong.
    const auto at = [&](float x, float y, float z) {
        return quebratsk::math::source_to_godot(godot::Vector3(x, y, z));
    };
    check("grid starts at its corner", near(surf.positions[4], at(0, 0, 100)));
    check("middle raised", near(surf.positions[4 + 12], at(32, 32, 110)));
    check("first row's bump", near(surf.positions[4 + 1], at(16, 0, 105)));
    check("grid's far corner", near(surf.positions[4 + 24], at(64, 64, 100)));
    // Its light: the grid's corners on the middles of the block's corner samples. The light's
    // s axis is the level's x, which the grid's columns follow.
    {
        const godot::Vector2 start = surf.uv1[4] * godot::Vector2(lm.width, lm.height);
        const int dx = static_cast<int>(std::floor(start.x));
        const int dy = static_cast<int>(std::floor(start.y));
        check("displacement light starts on a sample's middle",
              near(start.x - dx, 0.5f) && near(start.y - dy, 0.5f));
        check("displacement light, exponent -1", near(sample(dx, dy, 2), 35.0f / 255.0f));
        const godot::Vector2 row_end = surf.uv1[4 + 4] * godot::Vector2(lm.width, lm.height);
        const godot::Vector2 col_end = surf.uv1[4 + 20] * godot::Vector2(lm.width, lm.height);
        const godot::Vector2 far = surf.uv1[4 + 24] * godot::Vector2(lm.width, lm.height);
        check("end of the first row is one sample across", near(row_end.x, dx + 1.5f) && near(row_end.y, dy + 0.5f));
        check("start of the last row is one sample down", near(col_end.x, dx + 0.5f) && near(col_end.y, dy + 1.5f));
        check("grid's far corner on the last sample", near(far.x, dx + 1.5f) && near(far.y, dy + 1.5f));
    }
    // Its triangles face up, and so does its middle.
    bool all_up = true;
    for (size_t t = 6; t < surf.indices.size(); t += 3) {
        const auto& a = surf.positions[surf.indices[t]];
        const auto& b = surf.positions[surf.indices[t + 1]];
        const auto& c = surf.positions[surf.indices[t + 2]];
        if ((c - a).cross(b - a).y <= 0.0f) all_up = false;
    }
    check("displacement triangles face up", all_up);
    check("displacement middle normal up", surf.normals[4 + 12].y > 0.99f);

    // The prop: at its origin, turned 90 degrees to the left, which points its front (the
    // model's +x, Godot's -z) along the level's +y (Godot's -x).
    check("one prop", map.props.size() == 1 && map.prop_models.size() == 1);
    if (map.props.size() != 1) return;
    const VBSPStaticProp& prop = map.props[0];
    check("prop model name", prop.model == "models/props/crate.mdl");
    check("prop skin", prop.skin == 2);
    check("prop solid", prop.solid);
    check("prop lump version", map.prop_lump_version == 6);
    check("prop origin", near(prop.transform.origin, at(100, 200, 300)));
    check("prop faces its yaw", near(prop.transform.basis.xform(godot::Vector3(0, 0, -1)),
                                     godot::Vector3(-1, 0, 0)));
    check("prop stays upright", near(prop.transform.basis.xform(godot::Vector3(0, 1, 0)),
                                     godot::Vector3(0, 1, 0)));
    check("no pakfile", map.pakfile.empty());

    // Not a level, and a level from a version this reader does not know.
    std::vector<uint8_t> broken = file;
    broken[0] = 'X';
    check("wrong magic refused", !VBSPParser::parse(as_bytes(broken)).has_value());
    std::vector<uint8_t> later = file;
    const int32_t version = 29;
    std::memcpy(later.data() + 4, &version, 4);
    check("unknown version refused", !VBSPParser::parse(as_bytes(later)).has_value());
    check("truncated refused", !VBSPParser::parse(as_bytes(std::vector<uint8_t>(file.begin(), file.begin() + 100))).has_value());
}

// --------------------------------------------------------------------- real levels ----

static std::vector<uint8_t> read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

template <typename T>
static std::span<const T> lump_of(const std::vector<uint8_t>& file, int index) {
    VbspHeader header{};
    std::memcpy(&header, file.data(), sizeof(header));
    const auto& entry = header.lumps[index];
    if (entry.offset < 0 || entry.length <= 0 ||
        static_cast<size_t>(entry.offset) + entry.length > file.size()) {
        return {};
    }
    return {reinterpret_cast<const T*>(file.data() + entry.offset), entry.length / sizeof(T)};
}

/// The points along a displacement's four edges, pushed off the face, in the level's units,
/// with the grid run one way or the other.
static std::vector<godot::Vector3> edge_points(const std::vector<uint8_t>& file, const VbspFace& face,
                                               bool rows_along_first_edge) {
    const auto verts = lump_of<godot::Vector3>(file, kLumpVertexes);
    const auto edges = lump_of<VbspEdge>(file, kLumpEdges);
    const auto surfedges = lump_of<int32_t>(file, kLumpSurfEdges);
    const auto infos = lump_of<VbspDispInfo>(file, kLumpDispInfo);
    const auto dverts = lump_of<VbspDispVert>(file, kLumpDispVerts);
    if (face.num_edges != 4 || face.dispinfo < 0 || static_cast<size_t>(face.dispinfo) >= infos.size()) return {};
    godot::Vector3 ring[4];
    for (int e = 0; e < 4; ++e) {
        const int32_t s = surfedges[static_cast<size_t>(face.first_edge + e)];
        const auto& edge = edges[static_cast<size_t>(s >= 0 ? s : -s)];
        ring[e] = verts[s >= 0 ? edge.v[0] : edge.v[1]];
    }
    const VbspDispInfo& d = infos[static_cast<size_t>(face.dispinfo)];
    const godot::Vector3 start(d.start_position[0], d.start_position[1], d.start_position[2]);
    int first = 0;
    for (int i = 1; i < 4; ++i) {
        if (ring[i].distance_squared_to(start) < ring[first].distance_squared_to(start)) first = i;
    }
    const godot::Vector3 c0 = ring[first], c1 = ring[(first + 1) % 4];
    const godot::Vector3 c2 = ring[(first + 2) % 4], c3 = ring[(first + 3) % 4];
    const int side = (1 << d.power) + 1;
    std::vector<godot::Vector3> out;
    for (int row = 0; row < side; ++row) {
        for (int col = 0; col < side; ++col) {
            if (row != 0 && col != 0 && row != side - 1 && col != side - 1) continue;
            const float a = static_cast<float>(row) / (side - 1);
            const float b = static_cast<float>(col) / (side - 1);
            const godot::Vector3 base = rows_along_first_edge ? c0.lerp(c1, a).lerp(c3.lerp(c2, a), b)
                                                              : c0.lerp(c3, a).lerp(c1.lerp(c2, a), b);
            const auto& dv = dverts[static_cast<size_t>(d.disp_vert_start + row * side + col)];
            out.push_back(base + godot::Vector3(dv.vec[0], dv.vec[1], dv.vec[2]) * dv.dist);
        }
    }
    return out;
}

static int level(const std::filesystem::path& path) {
    const std::vector<uint8_t> file = read_file(path);
    const auto parsed = VBSPParser::parse(as_bytes(file));
    if (!parsed.has_value()) {
        std::printf("%s: not read (%d)\n", path.filename().string().c_str(), static_cast<int>(parsed.error()));
        return 1;
    }
    const ParsedVBSPMap& map = *parsed;
    const VBSPStats& s = map.stats;
    std::printf("%s: version %d, %zu models, %zu faces in file\n", path.filename().string().c_str(),
                map.version, map.brush_models.size(), s.faces_in_file);
    std::printf("  drawn %zu (of them %zu displacements, %zu bad), not drawn %zu, sky %zu, water %zu\n",
                s.faces_drawn, s.displacements, s.displacements_bad, s.faces_not_drawn, map.sky_faces,
                s.water_faces);
    std::printf("  textures %zu, lit %zu, unlit %zu, light %ux%u (%s), compressed lumps %zu\n",
                s.textures, s.lit_faces, s.unlit_faces, map.lightmap.width, map.lightmap.height,
                map.hdr_light ? "HDR" : "LDR", s.compressed_lumps);
    std::printf("  props %zu of %zu model(s), prop lump version %d, pakfile %zu bytes\n",
                map.props.size(), map.prop_models.size(), map.prop_lump_version, map.pakfile.size());

    // Every face in a model's range is either drawn or not drawn, and none is lost.
    const auto faces = lump_of<VbspFace>(file, map.hdr_light && !lump_of<VbspFace>(file, kLumpFacesHdr).empty()
                                                   ? kLumpFacesHdr : kLumpFaces);
    const auto models = lump_of<VbspModel>(file, kLumpModels);
    size_t owned = 0;
    for (const auto& m : models) owned += static_cast<size_t>(std::max(m.num_faces, 0));
    std::printf("  faces owned by a model %zu\n", owned);
    check("every face drawn or left out on purpose",
          s.faces_drawn + s.faces_not_drawn + s.displacements_bad == owned);

    // Flat faces face their planes.
    size_t facing = 0;
    size_t against = 0;
    size_t corners_inside = 0;
    size_t corners_outside = 0;
    for (const auto& model : map.brush_models) {
        for (const auto& surf : model.mesh.surfaces) {
            for (size_t t = 0; t + 2 < surf.indices.size(); t += 3) {
                const auto& a = surf.positions[surf.indices[t]];
                const auto& b = surf.positions[surf.indices[t + 1]];
                const auto& c = surf.positions[surf.indices[t + 2]];
                const godot::Vector3 front = (c - a).cross(b - a);
                // Slivers: a face's corners include points on its edges where a neighbour's
                // corner touches it, and a fan through three of those in a row has no
                // direction worth checking (and no area to see).
                if (front.length() <= 1e-3f * (c - a).length() * (b - a).length()) continue;
                // Only flat faces: a displacement's points carry smoothed normals, which a
                // steep fold can leave pointing away from one of its triangles.
                const godot::Vector3& na = surf.normals[surf.indices[t]];
                if (!near(na, surf.normals[surf.indices[t + 1]]) || !near(na, surf.normals[surf.indices[t + 2]])) {
                    continue;
                }
                if (front.dot(surf.normals[surf.indices[t]]) > 0.0f) ++facing; else ++against;
            }
        }
    }
    // Corners inside their own light block: projected, less the block's corner, between 0
    // and the block's size (in luxels).
    const auto infos = lump_of<VbspTexInfo>(file, kLumpTexInfo);
    const auto verts = lump_of<godot::Vector3>(file, kLumpVertexes);
    const auto edges = lump_of<VbspEdge>(file, kLumpEdges);
    const auto surfedges = lump_of<int32_t>(file, kLumpSurfEdges);
    size_t disp_inside = 0;
    size_t disp_outside = 0;
    for (const auto& face : faces) {
        if (face.light_offset < 0 || face.texinfo < 0) continue;
        const auto& info = infos[static_cast<size_t>(face.texinfo)];
        for (int e = 0; e < face.num_edges; ++e) {
            const int32_t sedge = surfedges[static_cast<size_t>(face.first_edge + e)];
            const auto& edge = edges[static_cast<size_t>(sedge >= 0 ? sedge : -sedge)];
            const godot::Vector3 p = verts[sedge >= 0 ? edge.v[0] : edge.v[1]];
            const auto proj = [&](const float* v) { return p.x * v[0] + p.y * v[1] + p.z * v[2] + v[3]; };
            const float u = proj(info.lightmap_vecs[0]) - face.lightmap_mins[0];
            const float v = proj(info.lightmap_vecs[1]) - face.lightmap_mins[1];
            const bool inside = u > -0.01f && v > -0.01f && u < face.lightmap_size[0] + 0.01f &&
                                v < face.lightmap_size[1] + 0.01f;
            if (face.dispinfo >= 0) {
                (inside ? disp_inside : disp_outside)++;
            } else {
                (inside ? corners_inside : corners_outside)++;
            }
        }
    }
    std::printf("  triangles facing their plane %zu, against %zu\n", facing, against);
    // A displacement's frame need not project inside its light (the light is laid over its
    // grid instead), so its count is only shown.
    std::printf("  flat corners inside their light %zu, outside %zu; displacement frames' corners %zu in, %zu out\n",
                corners_inside, corners_outside, disp_inside, disp_outside);
    check("triangles face their planes", against * 1000 <= facing);
    check("flat corners inside their light", corners_outside * 1000 <= corners_inside);

    // Which way a displacement's grid runs: the reading whose edges meet the most edges of
    // other displacements is the one the file was written in.
    if (s.displacements > 0) {
        size_t met[2] = {0, 0};
        for (int way = 0; way < 2; ++way) {
            std::map<std::tuple<long, long, long>, int> seen;
            std::vector<std::vector<godot::Vector3>> all;
            for (const auto& face : faces) {
                if (face.dispinfo < 0) continue;
                all.push_back(edge_points(file, face, way == 0));
            }
            for (size_t i = 0; i < all.size(); ++i) {
                for (const auto& p : all[i]) {
                    ++seen[{std::lround(p.x * 4), std::lround(p.y * 4), std::lround(p.z * 4)}];
                }
            }
            for (const auto& [key, n] : seen) {
                if (n > 1) met[way] += static_cast<size_t>(n);
            }
        }
        std::printf("  displacement edge points shared: rows along first edge %zu, other way %zu\n",
                    met[0], met[1]);
        check("displacement grid runs along its first edge", met[0] >= met[1]);
    }
    return 0;
}

static int corpus(const std::filesystem::path& root) {
    if (std::filesystem::is_regular_file(root)) {
        level(root);
    } else {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
            if (entry.is_regular_file() && entry.path().extension() == ".bsp") {
                const std::vector<uint8_t> head = read_file(entry.path());
                if (VBSPParser::is_vbsp(as_bytes(head))) level(entry.path());
            }
        }
    }
    std::printf(failures == 0 ? "all checks passed\n" : "%d check(s) failed\n", failures);
    return failures == 0 ? 0 : 1;
}

int main(int argc, char** argv) {
    if (argc > 1) return corpus(argv[1]);
    test_hand_built();
    std::printf(failures == 0 ? "all checks passed\n" : "%d check(s) failed\n", failures);
    return failures == 0 ? 0 : 1;
}
