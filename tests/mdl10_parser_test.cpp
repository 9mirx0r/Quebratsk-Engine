// Verification of the GoldSrc StudioMDL v10 mesh extractor.
//
// Builds a synthetic .mdl in memory and checks the decoded result. This exercises the
// parts that are easy to get silently wrong: the header field offsets, the bone
// hierarchy composition (GoldSrc stores vertices in BONE-LOCAL space), triangle-strip
// expansion, UV normalization against the texture size, and the embedded 8-bit
// palettized texture decode.
//
// Needs godot-cpp for Vector3/Basis/Transform3D, but no running engine: the parser
// touches no Object, no String and no Variant.
//
// Build (Developer Command Prompt, from the repository root, after `cmake --build
// build-release --config Release` so godot-cpp exists).
// /std:c++latest is required for std::expected; /MD must match how godot-cpp was built.
//
//   cl /nologo /std:c++latest /EHsc /MD /I src ^
//      /I build-release\_deps\godot-cpp-src\include ^
//      /I build-release\_deps\godot-cpp-build\gen\include ^
//      /I build-release\_deps\godot-cpp-src\gdextension ^
//      tests\mdl10_parser_test.cpp src\parsers\goldsrc\mdl10_parser.cpp ^
//      build-release\_deps\godot-cpp-build\bin\Release\godot-cpp.windows.release.64.lib ^
//      /Fe:mdl_test.exe
//   mdl_test.exe
//
// Exits non-zero on failure.

#include "parsers/goldsrc/mdl10_parser.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace quebratsk::parsers::goldsrc;

static int failures = 0;

static void check(const char* what, double got, double want, double tol = 1e-4) {
    if (std::fabs(got - want) > tol) {
        std::printf("  FAIL %-38s got %12.6f want %12.6f\n", what, got, want);
        ++failures;
    }
}
static void check_i(const char* what, long long got, long long want) {
    if (got != want) {
        std::printf("  FAIL %-38s got %lld want %lld\n", what, got, want);
        ++failures;
    }
}
static void check_s(const char* what, const std::string& got, const std::string& want) {
    if (got != want) {
        std::printf("  FAIL %-38s got '%s' want '%s'\n", what, got.c_str(), want.c_str());
        ++failures;
    }
}

// ---------------------------------------------------------------- file builder ----
struct Builder {
    std::vector<uint8_t> buf;

    void ensure(size_t n) { if (buf.size() < n) buf.resize(n, 0); }
    template <typename T> void put(size_t at, const T& v) {
        ensure(at + sizeof(T));
        std::memcpy(buf.data() + at, &v, sizeof(T));
    }
    void put_bytes(size_t at, const void* p, size_t n) {
        ensure(at + n);
        std::memcpy(buf.data() + at, p, n);
    }
    void put_str(size_t at, const char* s, size_t field) {
        ensure(at + field);
        std::memset(buf.data() + at, 0, field);
        std::memcpy(buf.data() + at, s, std::strlen(s));
    }
};

int main() {
    std::printf("GoldSrc MDL v10 parser verification\n\n");

    constexpr float kScale = 0.0254f; // kHammerUnitsToMeters

    // Offsets chosen so every table is naturally aligned and non-overlapping.
    constexpr size_t kOfsBones     = 224;
    constexpr size_t kOfsTextures  = kOfsBones + 2 * 112;          // 448
    constexpr size_t kOfsTexPixels = kOfsTextures + 80;            // 528
    constexpr size_t kOfsSkin      = kOfsTexPixels + 4 + 768;      // 1300
    constexpr size_t kOfsBodyParts = 1304;
    constexpr size_t kOfsModel     = kOfsBodyParts + 76;           // 1380
    constexpr size_t kOfsVerts     = kOfsModel + 112;              // 1492
    constexpr size_t kOfsNorms     = kOfsVerts + 36;               // 1528
    constexpr size_t kOfsVertInfo  = kOfsNorms + 36;               // 1564
    constexpr size_t kOfsNormInfo  = kOfsVertInfo + 3;             // 1567
    constexpr size_t kOfsMesh      = 1572;
    constexpr size_t kOfsTriCmds   = kOfsMesh + 20;                // 1592

    Builder b;

    // ---- header ----
    b.put_str(0, "IDST", 4);
    b.put<int32_t>(4, 10);                 // version
    b.put_str(8, "test_model.mdl", 64);
    b.put<int32_t>(140, 2);                // num_bones
    b.put<int32_t>(144, (int32_t)kOfsBones);
    b.put<int32_t>(180, 1);                // num_textures
    b.put<int32_t>(184, (int32_t)kOfsTextures);
    b.put<int32_t>(192, 1);                // num_skin_ref
    b.put<int32_t>(196, 1);                // num_skin_families
    b.put<int32_t>(200, (int32_t)kOfsSkin);
    b.put<int32_t>(204, 1);                // num_bodyparts
    b.put<int32_t>(208, (int32_t)kOfsBodyParts);

    // ---- bones: bone0 identity at origin, bone1 child translated +100 on X ----
    // StudioBone layout: name[32] @0, parent @32, flags @36,
    //                    bone_controller[6] @40, value[6] @64, scale[6] @88.
    constexpr size_t kBoneValue = 64;

    b.put_str(kOfsBones + 0, "root", 32);
    b.put<int32_t>(kOfsBones + 32, -1);    // parent
    for (int i = 0; i < 6; ++i) b.put<float>(kOfsBones + kBoneValue + 4 * i, 0.0f);

    b.put_str(kOfsBones + 112 + 0, "child", 32);
    b.put<int32_t>(kOfsBones + 112 + 32, 0); // parent = bone0
    b.put<float>(kOfsBones + 112 + kBoneValue + 0, 100.0f); // value[0] = x
    for (int i = 1; i < 6; ++i) b.put<float>(kOfsBones + 112 + kBoneValue + 4 * i, 0.0f);

    // ---- texture: 2x2, palette entry 0 = red, entry 1 = green ----
    b.put_str(kOfsTextures + 0, "skin.bmp", 64);
    b.put<int32_t>(kOfsTextures + 64, 0);                    // flags
    b.put<int32_t>(kOfsTextures + 68, 2);                    // width
    b.put<int32_t>(kOfsTextures + 72, 2);                    // height
    b.put<int32_t>(kOfsTextures + 76, (int32_t)kOfsTexPixels);

    const uint8_t px[4] = {0, 1, 0, 1};
    b.put_bytes(kOfsTexPixels, px, 4);
    std::vector<uint8_t> pal(768, 0);
    pal[0] = 255; pal[1] = 0;   pal[2] = 0;   // index 0 -> red
    pal[3] = 0;   pal[4] = 255; pal[5] = 0;   // index 1 -> green
    b.put_bytes(kOfsTexPixels + 4, pal.data(), pal.size());

    b.put<int16_t>(kOfsSkin, 0); // skin_ref 0 -> texture 0

    // ---- body part -> model ----
    b.put_str(kOfsBodyParts + 0, "body", 64);
    b.put<int32_t>(kOfsBodyParts + 64, 1);                 // num_models
    b.put<int32_t>(kOfsBodyParts + 68, 0);                 // base
    b.put<int32_t>(kOfsBodyParts + 72, (int32_t)kOfsModel);

    // StudioModel layout: name[64] @0, type @64, bounding_radius @68,
    //                     num_mesh @72, mesh_index @76, num_verts @80,
    //                     vert_info_index @84, vert_index @88, num_norms @92,
    //                     norm_info_index @96, norm_index @100.
    b.put_str(kOfsModel + 0, "model", 64);
    b.put<int32_t>(kOfsModel + 64, 0);                        // type
    b.put<float>(kOfsModel + 68, 1.0f);                       // bounding_radius
    b.put<int32_t>(kOfsModel + 72, 1);                        // num_mesh
    b.put<int32_t>(kOfsModel + 76, (int32_t)kOfsMesh);
    b.put<int32_t>(kOfsModel + 80, 3);                        // num_verts
    b.put<int32_t>(kOfsModel + 84, (int32_t)kOfsVertInfo);
    b.put<int32_t>(kOfsModel + 88, (int32_t)kOfsVerts);
    b.put<int32_t>(kOfsModel + 92, 3);                        // num_norms
    b.put<int32_t>(kOfsModel + 96, (int32_t)kOfsNormInfo);
    b.put<int32_t>(kOfsModel + 100, (int32_t)kOfsNorms);

    // vertices, in BONE-LOCAL space
    const float verts[9] = {
        10.0f, 20.0f, 30.0f,   // v0 on bone 0 -> model space unchanged
         0.0f,  0.0f,  0.0f,   // v1 on bone 1 -> model space (100, 0, 0)
         0.0f,  0.0f,  0.0f,   // v2 on bone 0 -> origin
    };
    b.put_bytes(kOfsVerts, verts, sizeof(verts));

    const float norms[9] = { 0,0,1,  0,0,1,  0,0,1 };
    b.put_bytes(kOfsNorms, norms, sizeof(norms));

    const uint8_t vinfo[3] = {0, 1, 0}; // v1 is attached to bone 1
    b.put_bytes(kOfsVertInfo, vinfo, 3);
    const uint8_t ninfo[3] = {0, 0, 0};
    b.put_bytes(kOfsNormInfo, ninfo, 3);

    // ---- mesh + triangle command stream ----
    b.put<int32_t>(kOfsMesh + 0, 1);                       // num_tris
    b.put<int32_t>(kOfsMesh + 4, (int32_t)kOfsTriCmds);
    b.put<int32_t>(kOfsMesh + 8, 0);                       // skin_ref
    b.put<int32_t>(kOfsMesh + 12, 3);                      // num_norms
    b.put<int32_t>(kOfsMesh + 16, (int32_t)kOfsNorms);

    // One strip of 3 corners: (vertex, normal, s, t)
    size_t p = kOfsTriCmds;
    b.put<int16_t>(p, 3); p += 2;                              // positive = strip
    const int16_t corners[3][4] = {
        {0, 0, 0, 0},
        {1, 1, 2, 0},
        {2, 2, 0, 2},
    };
    for (auto& c : corners) { b.put_bytes(p, c, 8); p += 8; }
    b.put<int16_t>(p, 0); p += 2;                              // end marker

    // ------------------------------------------------------------------ parse ----
    std::span<const std::byte> data(reinterpret_cast<const std::byte*>(b.buf.data()), b.buf.size());
    auto res = MDL10Parser::parse(data);

    if (!res.has_value()) {
        std::printf("  FAIL parse() returned an error\n");
        return 1;
    }

    const auto& mesh = res->mesh_data;
    const auto& skel = res->skeleton_data;

    std::printf("Header / skeleton:\n");
    check_s("model name", mesh.name, "test_model.mdl");
    check_i("bone count", (long long)skel.bones.size(), 2);
    if (skel.bones.size() == 2) {
        check_s("bone 0 name", skel.bones[0].name, "root");
        check_s("bone 1 name", skel.bones[1].name, "child");
        check_i("bone 1 parent", skel.bones[1].parent_index, 0);
    }

    std::printf("Mesh:\n");
    check_i("surface count", (long long)mesh.surfaces.size(), 1);
    if (mesh.surfaces.empty()) {
        std::printf("\nFAILURES (%d)\n", failures + 1);
        return 1;
    }

    const auto& s = mesh.surfaces[0];
    check_s("material name", s.material_name, "skin.bmp");
    check_i("vertex count", (long long)s.positions.size(), 3);
    check_i("index count", (long long)s.indices.size(), 3);
    check_i("normal count", (long long)s.normals.size(), 3);
    check_i("uv count", (long long)s.uv0.size(), 3);

    if (s.positions.size() == 3) {
        std::printf("Bone-local -> model space:\n");
        // v0 on bone 0 (identity): (10,20,30) -> godot (x*k, z*k, -y*k)
        check("v0.x", s.positions[0].x,  10.0f * kScale);
        check("v0.y", s.positions[0].y,  30.0f * kScale);
        check("v0.z", s.positions[0].z, -20.0f * kScale);
        // v1 on bone 1, whose rest transform translates +100 on X.
        // A parser that ignored the hierarchy would leave this at the origin.
        check("v1.x (bone hierarchy)", s.positions[1].x, 100.0f * kScale);
        check("v1.y", s.positions[1].y, 0.0f);
        check("v1.z", s.positions[1].z, 0.0f);
        // v2 on bone 0 at local origin
        check("v2.x", s.positions[2].x, 0.0f);
    }

    if (s.uv0.size() == 3) {
        std::printf("UV normalization (2x2 texture):\n");
        check("uv0.u", s.uv0[0].x, 0.0);
        check("uv0.v", s.uv0[0].y, 0.0);
        check("uv1.u", s.uv0[1].x, 1.0);   // s=2 / width=2
        check("uv2.v", s.uv0[2].y, 1.0);   // t=2 / height=2
    }

    if (s.indices.size() == 3) {
        std::printf("Strip expansion:\n");
        check_i("idx0", s.indices[0], 0);
        check_i("idx1", s.indices[1], 1);
        check_i("idx2", s.indices[2], 2);
    }

    std::printf("Embedded texture:\n");
    check_i("embedded texture count", (long long)mesh.embedded_textures.size(), 1);
    check_i("surface -> embedded index", s.embedded_texture_index, 0);
    if (!mesh.embedded_textures.empty()) {
        const auto& t = mesh.embedded_textures[0];
        check_i("tex width", t.width, 2);
        check_i("tex height", t.height, 2);
        check_i("tex valid", t.is_valid() ? 1 : 0, 1);
        if (t.rgba8_pixels.size() >= 8) {
            check_i("px0 r (palette 0 = red)", t.rgba8_pixels[0], 255);
            check_i("px0 g", t.rgba8_pixels[1], 0);
            check_i("px1 g (palette 1 = green)", t.rgba8_pixels[5], 255);
            check_i("px1 r", t.rgba8_pixels[4], 0);
            check_i("px0 a (opaque)", t.rgba8_pixels[3], 255);
        }
    }

    std::printf("Skinning:\n");
    check_i("bone_indices parallel", (long long)s.bone_indices.size(), (long long)s.positions.size());
    check_i("bone_weights parallel", (long long)s.bone_weights.size(), (long long)s.positions.size());
    if (s.bone_indices.size() == 3) {
        check_i("v1 bone id", s.bone_indices[1][0], 1);
        check("v1 weight", s.bone_weights[1][0], 1.0);
    }

    std::printf("\n%s (%d failure%s)\n", failures == 0 ? "ALL PASS" : "FAILURES",
                failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
