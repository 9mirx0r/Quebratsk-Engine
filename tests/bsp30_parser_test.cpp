// Verification of the BSP30 lump layout detection.
//
// Blue Shift stores planes in lump 0 and entities in lump 1; every other GoldSrc
// game stores them the other way round, under the same version number. The parser
// used to read lump 0 as entities unconditionally, so all 37 Blue Shift maps came
// back with plane bytes as their entity text. These cases build a minimal map in
// each layout and check the entity text survives.
//
// The second half covers baked light: one lit rectangle, built by hand, whose light
// grid must land in the atlas with the right size, the right values and the right
// coordinates on the face.
//
// The third part covers collision: small trees built by hand, whose solid leaves must come
// back as convex blocks of the right number, shape and size, and broken trees that must
// come back as nothing rather than hang or crash.
//
// No framework: prints its checks and exits non-zero on failure.

#include "parsers/goldsrc/bsp30_parser.h"
#include "parsers/goldsrc/bsp30_solids.h"

#include "core/math/axis_remap.h"
#include "parsers/goldsrc/structs/bsp30_structs.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <string>
#include <vector>

using namespace quebratsk::parsers::goldsrc;

static int failures = 0;

static void check(const char* what, bool ok) {
    if (!ok) {
        std::printf("  FAIL %s\n", what);
        ++failures;
    }
}

// A map with only an entities lump and a planes lump (one 20-byte plane).
static std::vector<uint8_t> build(const std::string& entities, bool blue_shift, int32_t version = 30) {
    constexpr size_t kHeader = 4 + 15 * 8;
    std::vector<uint8_t> plane(20, 0);
    const float normal_z = 1.0f;
    std::memcpy(plane.data() + 8, &normal_z, 4);

    std::vector<uint8_t> buf(kHeader, 0);
    std::memcpy(buf.data(), &version, 4);
    auto add_lump = [&](size_t index, const void* data, size_t size) {
        const int32_t offset = static_cast<int32_t>(buf.size());
        const int32_t length = static_cast<int32_t>(size);
        buf.insert(buf.end(), static_cast<const uint8_t*>(data),
                   static_cast<const uint8_t*>(data) + size);
        std::memcpy(buf.data() + 4 + index * 8, &offset, 4);
        std::memcpy(buf.data() + 4 + index * 8 + 4, &length, 4);
    };
    add_lump(blue_shift ? 1 : 0, entities.data(), entities.size());
    add_lump(blue_shift ? 0 : 1, plane.data(), plane.size());
    return buf;
}

static auto parse(const std::vector<uint8_t>& buf) {
    return BSP30Parser::parse(std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(buf.data()), buf.size()));
}

// A map with a single face: a rectangle 32 units along S and 16 along T, lying on the
// texture axes. That is a light grid of 3 by 2 samples (one every 16 units, both ends
// included). `light` is lump 8 as it would sit in the file; an empty one means a map
// compiled without light.
static std::vector<uint8_t> build_lit(const std::vector<uint8_t>& light, int32_t light_offset,
                                      uint8_t style0, uint8_t style1,
                                      const std::string& more_entities = {}) {
    constexpr size_t kHeader = 4 + 15 * 8;
    const std::string entities = "{\n\"classname\" \"worldspawn\"\n}\n" + more_entities;
    const int32_t version = 30;

    std::vector<uint8_t> buf(kHeader, 0);
    std::memcpy(buf.data(), &version, 4);
    auto add_lump = [&](size_t index, const void* data, size_t size) {
        const int32_t offset = static_cast<int32_t>(buf.size());
        const int32_t length = static_cast<int32_t>(size);
        buf.insert(buf.end(), static_cast<const uint8_t*>(data),
                   static_cast<const uint8_t*>(data) + size);
        std::memcpy(buf.data() + 4 + index * 8, &offset, 4);
        std::memcpy(buf.data() + 4 + index * 8 + 4, &length, 4);
    };

    const BSPPlane plane{{0.0f, 0.0f, 1.0f}, 0.0f, 2};
    const float vertices[4][3] = {{0, 0, 0}, {32, 0, 0}, {32, 16, 0}, {0, 16, 0}};
    // Edge 0 is never referenced: a surfedge of 0 cannot say which way it runs.
    const BSPEdge edges[5] = {{{0, 0}}, {{0, 1}}, {{1, 2}}, {{2, 3}}, {{3, 0}}};
    const int32_t surfedges[4] = {1, 2, 3, 4};
    BSPTexInfo texinfo{};
    texinfo.vector_s[0] = 1.0f;   // S runs along X
    texinfo.vector_t[1] = 1.0f;   // T runs along Y
    BSPFace face{};
    face.num_edges = 4;
    face.styles[0] = style0;
    face.styles[1] = style1;
    face.styles[2] = 255;
    face.styles[3] = 255;
    face.lightmap_offset = light_offset;

    add_lump(0, entities.data(), entities.size());
    add_lump(1, &plane, sizeof(plane));
    add_lump(3, vertices, sizeof(vertices));
    add_lump(6, &texinfo, sizeof(texinfo));
    add_lump(7, &face, sizeof(face));
    if (!light.empty()) add_lump(8, light.data(), light.size());
    add_lump(12, edges, sizeof(edges));
    add_lump(13, surfedges, sizeof(surfedges));
    return buf;
}

// The corners of the face's light coordinates, in atlas pixels.
struct LightBox {
    float min_x = 1e9f, min_y = 1e9f, max_x = -1e9f, max_y = -1e9f;
    size_t count = 0;
};

static LightBox light_box(const ParsedBSP30Map& map) {
    LightBox box;
    for (const auto& surf : map.mesh_data.surfaces) {
        for (const auto& uv : surf.uv1) {
            const float x = uv.x * static_cast<float>(map.lightmap.width);
            const float y = uv.y * static_cast<float>(map.lightmap.height);
            box.min_x = std::min(box.min_x, x);
            box.max_x = std::max(box.max_x, x);
            box.min_y = std::min(box.min_y, y);
            box.max_y = std::max(box.max_y, y);
            ++box.count;
        }
    }
    return box;
}

static bool near(float a, float b) { return std::fabs(a - b) < 0.01f; }

static const uint8_t* atlas_pixel(const ParsedBSP30Map& map, uint32_t x, uint32_t y) {
    return map.lightmap.rgba8_pixels.data() + (static_cast<size_t>(y) * map.lightmap.width + x) * 4;
}

static void light_cases() {
    // Lump 8 with three bytes of padding first, so a parser that ignores the face's offset
    // reads the wrong samples. Then style A's grid (3 x 2 samples, R = 10, 20 .. 60, G = 1,
    // B = 2) and style B's grid (R = 200 throughout).
    std::vector<uint8_t> light = {9, 9, 9};
    for (int i = 0; i < 6; ++i) {
        light.push_back(static_cast<uint8_t>(10 * (i + 1)));
        light.push_back(1);
        light.push_back(2);
    }
    for (int i = 0; i < 6; ++i) {
        light.push_back(200);
        light.push_back(0);
        light.push_back(0);
    }

    {
        auto parsed = parse(build_lit(light, 3, /*style0*/ 0, /*style1*/ 255));
        check("one style: parse ok", parsed.has_value());
        if (parsed) {
            const auto& lm = parsed->lightmap;
            check("one style: atlas exists", lm.is_valid() && lm.width == 256 && lm.height == 4);
            check("one style: atlas pixel count",
                  lm.rgba8_pixels.size() == static_cast<size_t>(lm.width) * lm.height * 4);
            if (lm.is_valid() && lm.height == 4) {
                // The white patch takes the first 4 x 4 pixels; the grid sits right after.
                const uint8_t* white = atlas_pixel(*parsed, 0, 0);
                check("one style: white patch", white[0] == 255 && white[1] == 255 && white[2] == 255);
                const uint8_t* first = atlas_pixel(*parsed, 4, 0);
                const uint8_t* last = atlas_pixel(*parsed, 6, 1);
                check("one style: first sample", first[0] == 10 && first[1] == 1 && first[2] == 2);
                check("one style: last sample", last[0] == 60 && last[1] == 1 && last[2] == 2);
                check("one style: opaque", first[3] == 255);

                // Corners sit on the centres of the grid's outer samples.
                const LightBox box = light_box(*parsed);
                check("one style: every vertex has light coordinates", box.count >= 4);
                check("one style: light coordinates",
                      near(box.min_x, 4.5f) && near(box.max_x, 6.5f) &&
                      near(box.min_y, 0.5f) && near(box.max_y, 1.5f));
            }
        }
    }
    {
        // Two styles: the grids are stored one after the other and are added together,
        // stopping at white rather than wrapping round.
        auto parsed = parse(build_lit(light, 3, 0, 1));
        check("two styles: parse ok", parsed && parsed->lightmap.is_valid());
        if (parsed && parsed->lightmap.is_valid() && parsed->lightmap.height == 4) {
            check("two styles: summed", atlas_pixel(*parsed, 4, 0)[0] == 210);
            check("two styles: clamped", atlas_pixel(*parsed, 6, 1)[0] == 255);
            check("two styles: other channels", atlas_pixel(*parsed, 4, 0)[1] == 1);
        }
    }
    {
        // A named light is baked under a style of its own (32 and up). Flagged "initially
        // dark" it is off when the level starts, so its grid is left out; without the flag,
        // or without a name, it counts like any other.
        const auto lamp = [](const char* extra) {
            return std::string("{\n\"classname\" \"light\"\n\"style\" \"32\"\n") + extra + "}\n";
        };
        const auto red = [&](const std::string& entities) -> int {
            auto parsed = parse(build_lit(light, 3, 0, 32, entities));
            if (!parsed || !parsed->lightmap.is_valid() || parsed->lightmap.height != 4) return -1;
            return atlas_pixel(*parsed, 4, 0)[0];
        };
        check("switchable light: on", red(lamp("\"targetname\" \"lamp\"\n")) == 210);
        check("switchable light: starts dark",
              red(lamp("\"targetname\" \"lamp\"\n\"spawnflags\" \"1\"\n")) == 10);
        check("switchable light: flag without a name", red(lamp("\"spawnflags\" \"1\"\n")) == 210);
        check("switchable light: another style's flag",
              red("{\n\"classname\" \"light\"\n\"style\" \"33\"\n\"targetname\" \"x\"\n"
                  "\"spawnflags\" \"1\"\n}\n") == 210);
        check("switchable light: not a light",
              red("{\n\"classname\" \"func_door\"\n\"style\" \"32\"\n\"targetname\" \"x\"\n"
                  "\"spawnflags\" \"1\"\n}\n") == 210);
    }
    {
        // A grid that would run past the end of the lump is not read; the face falls back
        // to the white patch, sampled at its middle.
        auto parsed = parse(build_lit(light, static_cast<int32_t>(light.size()) - 6, 0, 255));
        check("offset past the end: parse ok", parsed && parsed->lightmap.is_valid());
        if (parsed && parsed->lightmap.is_valid()) {
            const LightBox box = light_box(*parsed);
            check("offset past the end: white patch",
                  box.count >= 4 && near(box.min_x, 2.0f) && near(box.max_x, 2.0f) &&
                  near(box.min_y, 2.0f) && near(box.max_y, 2.0f));
        }
    }
    {
        // Offset -1 is how the format says "this face has no light of its own".
        auto parsed = parse(build_lit(light, -1, 0, 255));
        check("no light on the face: parse ok", parsed && parsed->lightmap.is_valid());
        if (parsed && parsed->lightmap.is_valid()) {
            const LightBox box = light_box(*parsed);
            check("no light on the face: white patch",
                  box.count >= 4 && near(box.max_x, 2.0f) && near(box.max_y, 2.0f));
        }
    }
    {
        // A map compiled without light: no atlas, and no light coordinates either.
        auto parsed = parse(build_lit({}, 0, 0, 255));
        check("unlit map: parse ok", parsed.has_value());
        if (parsed) {
            check("unlit map: no atlas", !parsed->lightmap.is_valid());
            check("unlit map: face still drawn", !parsed->mesh_data.surfaces.empty());
            check("unlit map: no light coordinates", light_box(*parsed).count == 0);
        }
    }
}

// --- Collision -------------------------------------------------------------------------

constexpr int16_t kLeafSolid = -1;   // leaf 0
constexpr int16_t kLeafEmpty = -2;   // leaf 1
constexpr int16_t kLeafSky = -3;     // leaf 2
constexpr int16_t kLeafWater = -4;   // leaf 3

static std::vector<BSPLeaf> solid_leaves() {
    std::vector<BSPLeaf> leaves(4);
    leaves[0].contents = -2;   // solid
    leaves[1].contents = -1;   // empty
    leaves[2].contents = -6;   // sky
    leaves[3].contents = -3;   // water
    return leaves;
}

static BSPNode make_node(int32_t plane, int16_t front, int16_t back) {
    BSPNode node{};
    node.planenum = plane;
    node.children[0] = front;
    node.children[1] = back;
    return node;
}

// Plane 0 is the floor (z = 0, facing up), plane 1 a wall (x = 0, facing +x), plane 2 a
// wall at 45 degrees through the origin.
static std::vector<BSPPlane> solid_planes() {
    return {BSPPlane{{0.0f, 0.0f, 1.0f}, 0.0f, 2}, BSPPlane{{1.0f, 0.0f, 0.0f}, 0.0f, 0},
            BSPPlane{{0.70710678f, 0.70710678f, 0.0f}, 0.0f, 3}};
}

static std::vector<quebratsk::ir::IRConvexHull> solids_of(const std::vector<BSPNode>& nodes,
                                                          int32_t head = 0, float margin = 0.0f) {
    const auto leaves = solid_leaves();
    const auto planes = solid_planes();
    const float mins[3] = {-64.0f, -64.0f, -64.0f};
    const float maxs[3] = {64.0f, 64.0f, 64.0f};
    return build_model_solids(nodes, leaves, planes, head, mins, maxs, margin);
}

// Whether a block's corners span exactly the box lo..hi, given in map units.
static bool spans(const quebratsk::ir::IRConvexHull& hull, float lx, float ly, float lz, float hx,
                  float hy, float hz) {
    const godot::Vector3 a = quebratsk::math::source_to_godot(godot::Vector3(lx, ly, lz));
    const godot::Vector3 b = quebratsk::math::source_to_godot(godot::Vector3(hx, hy, hz));
    float lo[3] = {1e9f, 1e9f, 1e9f}, hi[3] = {-1e9f, -1e9f, -1e9f};
    for (const auto& v : hull.vertices) {
        const float p[3] = {v.x, v.y, v.z};
        for (int i = 0; i < 3; ++i) {
            lo[i] = std::min(lo[i], p[i]);
            hi[i] = std::max(hi[i], p[i]);
        }
    }
    const float ea[3] = {a.x, a.y, a.z}, eb[3] = {b.x, b.y, b.z};
    for (int i = 0; i < 3; ++i) {
        if (std::fabs(lo[i] - std::min(ea[i], eb[i])) > 1e-4f) return false;
        if (std::fabs(hi[i] - std::max(ea[i], eb[i])) > 1e-4f) return false;
    }
    return true;
}

static void solid_cases() {
    {
        // Solid below the floor, air above: the lower half of the box, as one block.
        const auto out = solids_of({make_node(0, kLeafEmpty, kLeafSolid)});
        check("floor: one block", out.size() == 1);
        if (out.size() == 1) {
            check("floor: eight corners", out[0].vertices.size() == 8);
            check("floor: lower half", spans(out[0], -64, -64, -64, 64, 64, 0));
        }
    }
    {
        // The other way up: a ceiling. Catches the two children being taken backwards.
        const auto out = solids_of({make_node(0, kLeafSolid, kLeafEmpty)});
        check("ceiling: one block", out.size() == 1);
        if (out.size() == 1) check("ceiling: upper half", spans(out[0], -64, -64, 0, 64, 64, 64));
    }
    {
        // Solid on both sides: kept whole, not cut in two.
        const auto out = solids_of({make_node(0, kLeafSolid, kLeafSolid)});
        check("all solid: one block", out.size() == 1);
        if (out.size() == 1) check("all solid: whole box", spans(out[0], -64, -64, -64, 64, 64, 64));
    }
    {
        const auto out = solids_of({make_node(0, kLeafEmpty, kLeafSky)});
        check("sky: counts as solid", out.size() == 1);
    }
    {
        const auto out = solids_of({make_node(0, kLeafEmpty, kLeafWater)});
        check("water: not solid", out.empty());
    }
    {
        // An L: solid below the floor, and above it solid only where x < 0.
        const auto out =
            solids_of({make_node(0, 1, kLeafSolid), make_node(1, kLeafEmpty, kLeafSolid)});
        check("two cuts: two blocks", out.size() == 2);
        if (out.size() == 2) {
            const bool a = spans(out[0], -64, -64, 0, 0, 64, 64) && spans(out[1], -64, -64, -64, 64, 64, 0);
            const bool b = spans(out[1], -64, -64, 0, 0, 64, 64) && spans(out[0], -64, -64, -64, 64, 64, 0);
            check("two cuts: shapes", a || b);
        }
    }
    {
        // A slanted wall cuts the box corner to corner: a wedge with six corners.
        const auto out = solids_of({make_node(2, kLeafEmpty, kLeafSolid)});
        check("slanted: one block", out.size() == 1);
        if (out.size() == 1) check("slanted: six corners", out[0].vertices.size() == 6);
    }
    {
        // The margin widens the box the blocks are cut from.
        const auto out = solids_of({make_node(0, kLeafEmpty, kLeafSolid)}, 0, 32.0f);
        check("margin: one block", out.size() == 1);
        if (out.size() == 1) check("margin: widened", spans(out[0], -96, -96, -96, 96, 96, 0));
    }
    {
        // Broken trees give nothing, and give it promptly.
        check("loop: nothing", solids_of({make_node(0, 0, 0)}).empty());
        check("long loop: nothing", solids_of({make_node(0, 1, 1), make_node(1, 0, 0)}).empty());
        check("child out of range: nothing", solids_of({make_node(0, 7, 7)}).empty());
        check("leaf out of range: nothing", solids_of({make_node(0, -9, -9)}).empty());
        check("plane out of range: nothing", solids_of({make_node(40, kLeafEmpty, kLeafSolid)}).empty());
        check("head out of range: nothing", solids_of({make_node(0, kLeafEmpty, kLeafSolid)}, 5).empty());
        check("no nodes: nothing", solids_of({}).empty());
        // A model whose head is a leaf is one block of whatever the leaf holds.
        check("head is a solid leaf: one block", solids_of({}, kLeafSolid).size() == 1);
        check("head is an empty leaf: nothing", solids_of({}, kLeafEmpty).empty());
    }
    {
        // The player's trees say what fills a side directly, with no leaves in between.
        const auto planes = solid_planes();
        const float mins[3] = {-64.0f, -64.0f, -64.0f};
        const float maxs[3] = {64.0f, 64.0f, 64.0f};
        const auto clip = [&](const std::vector<BSPClipNode>& nodes, int32_t head = 0) {
            return build_clip_solids(nodes, planes, head, mins, maxs, 0.0f);
        };
        const auto floor = clip({BSPClipNode{0, {-1, -2}}});
        check("player tree: one block", floor.size() == 1);
        if (floor.size() == 1) {
            check("player tree: lower half", spans(floor[0], -64, -64, -64, 64, 64, 0));
        }
        const auto corner = clip({BSPClipNode{0, {1, -2}}, BSPClipNode{1, {-1, -2}}});
        check("player tree: two blocks", corner.size() == 2);
        check("player tree: water is not solid", clip({BSPClipNode{0, {-1, -3}}}).empty());
        check("player tree: loop", clip({BSPClipNode{0, {0, 0}}}).empty());
        check("player tree: child out of range", clip({BSPClipNode{0, {9, 9}}}).empty());
        // A head equal to the number of clipnodes is how a model says it has no such tree.
        check("player tree: no tree for this model", clip({BSPClipNode{0, {-1, -2}}}, 1).empty());
        check("player tree: no clipnodes", clip({}).empty());
    }
    {
        // Through the parser: a map with the floor tree and one model gets the block on the
        // world, and the same one on brush model 0, with the world's wider margin.
        constexpr size_t kHeader = 4 + 15 * 8;
        const std::string entities = "{\n\"classname\" \"worldspawn\"\n}\n";
        const int32_t version = 30;
        std::vector<uint8_t> buf(kHeader, 0);
        std::memcpy(buf.data(), &version, 4);
        auto add_lump = [&](size_t index, const void* data, size_t size) {
            const int32_t offset = static_cast<int32_t>(buf.size());
            const int32_t length = static_cast<int32_t>(size);
            buf.insert(buf.end(), static_cast<const uint8_t*>(data),
                       static_cast<const uint8_t*>(data) + size);
            std::memcpy(buf.data() + 4 + index * 8, &offset, 4);
            std::memcpy(buf.data() + 4 + index * 8 + 4, &length, 4);
        };
        const auto planes = solid_planes();
        const auto leaves = solid_leaves();
        const BSPNode node = make_node(0, kLeafEmpty, kLeafSolid);
        BSPModel model{};
        for (int i = 0; i < 3; ++i) {
            model.mins[i] = -64.0f;
            model.maxs[i] = 64.0f;
        }
        add_lump(0, entities.data(), entities.size());
        add_lump(1, planes.data(), planes.size() * sizeof(BSPPlane));
        add_lump(5, &node, sizeof(node));
        // The player's tree: solid below z = 0 as well, read from head_nodes[1].
        const BSPClipNode clipnodes[2] = {{1, {-1, -1}}, {0, {-1, -2}}};
        model.head_nodes[1] = 1;
        add_lump(9, clipnodes, sizeof(clipnodes));
        add_lump(10, leaves.data(), leaves.size() * sizeof(BSPLeaf));
        add_lump(14, &model, sizeof(model));

        auto parsed = parse(buf);
        check("map: parse ok", parsed.has_value());
        if (parsed) {
            check("map: world block", parsed->collision_data.convex_hulls.size() == 1);
            check("map: same on model 0", parsed->brush_models.size() == 1 &&
                                              parsed->brush_models[0].solids.size() == 1);
            check("map: player block", parsed->brush_models.size() == 1 &&
                                           parsed->brush_models[0].player_solids.size() == 1);
            if (parsed->collision_data.convex_hulls.size() == 1) {
                check("map: world margin",
                      spans(parsed->collision_data.convex_hulls[0], -320, -320, -320, 320, 320, 0));
            }
        }
    }
    {
        // A map with no tree at all still parses, with nothing solid.
        auto parsed = parse(build("{\n\"classname\" \"worldspawn\"\n}\n", false));
        check("no tree: nothing solid", parsed && parsed->collision_data.convex_hulls.empty());
    }
}

int main() {
    std::printf("BSP30 lump layout verification\n\n");
    const std::string entities = "{\n\"classname\" \"worldspawn\"\n}\n";

    {
        auto parsed = parse(build(entities, /*blue_shift*/ false));
        check("standard: parse ok", parsed.has_value());
        if (parsed) {
            check("standard: entity text", parsed->entity_string.rfind(entities, 0) == 0);
            check("standard: not flagged blue shift", !parsed->blue_shift_layout);
        }
    }
    {
        auto parsed = parse(build(entities, /*blue_shift*/ true));
        check("blue shift: parse ok", parsed.has_value());
        if (parsed) {
            check("blue shift: entity text", parsed->entity_string.rfind(entities, 0) == 0);
            check("blue shift: flagged", parsed->blue_shift_layout);
        }
    }
    {
        // Leading whitespace before the first brace must not defeat detection.
        auto parsed = parse(build("\r\n  " + entities, /*blue_shift*/ true));
        check("blue shift, leading whitespace: flagged", parsed && parsed->blue_shift_layout);
    }
    {
        auto parsed = parse(build(entities, false, /*version*/ 29));
        check("wrong version: rejected",
              !parsed && parsed.error() == BSP30ParseError::VersionMismatch);
    }

    light_cases();
    solid_cases();

    if (failures == 0) {
        std::printf("all checks passed\n");
        return 0;
    }
    std::printf("%d check(s) failed\n", failures);
    return 1;
}
