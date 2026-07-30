// Verification of the Source 1 three-file model pipeline (.mdl + .vvd + .dx90.vtx).
//
// Two things here are easy to get silently wrong, and both are exercised:
//
//   1. The .vvd fixup table. When present, the on-disk vertex array is NOT in mesh
//      order and must be rebuilt by copying the runs it describes.
//   2. Every offset in a .vtx is relative to the address of the struct that contains
//      it, not to the start of the file. Treating them as absolute silently walks into
//      neighbouring structures.
//
// Build (Developer Command Prompt, from the repository root, after
// `cmake --build build-release --config Release`):
//
//   cl /nologo /std:c++latest /EHsc /MD /I src ^
//      /I build-release\_deps\godot-cpp-src\include ^
//      /I build-release\_deps\godot-cpp-build\gen\include ^
//      /I build-release\_deps\godot-cpp-src\gdextension ^
//      tests\source_mdl_test.cpp src\parsers\source1\mdl_source_parser.cpp ^
//      src\parsers\source1\vvd_parser.cpp ^
//      build-release\_deps\godot-cpp-build\bin\Release\godot-cpp.windows.release.64.lib ^
//      /Fe:src_test.exe
//   src_test.exe
//
// Exits non-zero on failure.

#include "parsers/source1/mdl_source_parser.h"
#include "parsers/source1/vvd_parser.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace quebratsk::parsers::source1;

static int failures = 0;

static void check(const char* what, double got, double want, double tol = 1e-4) {
    if (std::fabs(got - want) > tol) {
        std::printf("  FAIL %-40s got %12.6f want %12.6f\n", what, got, want);
        ++failures;
    }
}
static void check_i(const char* what, long long got, long long want) {
    if (got != want) {
        std::printf("  FAIL %-40s got %lld want %lld\n", what, got, want);
        ++failures;
    }
}
static void check_s(const char* what, const std::string& got, const std::string& want) {
    if (got != want) {
        std::printf("  FAIL %-40s got '%s' want '%s'\n", what, got.c_str(), want.c_str());
        ++failures;
    }
}

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
    std::span<const std::byte> span() const {
        return {reinterpret_cast<const std::byte*>(buf.data()), buf.size()};
    }
};

// ------------------------------------------------------------------ VVD builder ----
static Builder make_vvd(int32_t checksum, const std::vector<std::array<float, 3>>& positions,
                        const std::vector<VVDFixup>& fixups) {
    Builder v;
    constexpr size_t kVertexStart = 64;
    const size_t fixup_start = kVertexStart + positions.size() * sizeof(VVDVertex);

    v.put<int32_t>(0, kVvdMagic);
    v.put<int32_t>(4, kVvdVersion);
    v.put<int32_t>(8, checksum);
    v.put<int32_t>(12, 1);                                    // num_lods
    v.put<int32_t>(16, (int32_t)positions.size());            // num_lod_vertexes[0]
    v.put<int32_t>(48, (int32_t)fixups.size());               // num_fixups
    v.put<int32_t>(52, fixups.empty() ? 0 : (int32_t)fixup_start);
    v.put<int32_t>(56, (int32_t)kVertexStart);                // vertex_data_start
    v.put<int32_t>(60, 0);                                    // tangent_data_start

    for (size_t i = 0; i < positions.size(); ++i) {
        VVDVertex vert{};
        vert.bone_weights.weight[0] = 1.0f;
        vert.bone_weights.bone[0] = 0;
        vert.bone_weights.num_bones = 1;
        vert.position[0] = positions[i][0];
        vert.position[1] = positions[i][1];
        vert.position[2] = positions[i][2];
        vert.normal[2] = 1.0f;
        vert.tex_coord[0] = static_cast<float>(i);
        vert.tex_coord[1] = 0.0f;
        v.put(kVertexStart + i * sizeof(VVDVertex), vert);
    }

    for (size_t i = 0; i < fixups.size(); ++i) {
        v.put(fixup_start + i * sizeof(VVDFixup), fixups[i]);
    }
    return v;
}

int main() {
    std::printf("Source 1 .mdl + .vvd + .vtx verification\n\n");

    constexpr float kScale = 0.0254f;
    constexpr int32_t kChecksum = 0x12345678;

    // ================================================================ VVD alone ====
    std::printf("VVD without fixups:\n");
    {
        auto v = make_vvd(kChecksum, {{{1, 0, 0}}, {{2, 0, 0}}, {{3, 0, 0}}}, {});
        auto res = VVDParser::load_vertices(v.span(), 0);
        if (!res.has_value()) {
            std::printf("  FAIL load_vertices returned an error\n"); ++failures;
        } else {
            check_i("vertex count", (long long)res->size(), 3);
            check("v0.x", (*res)[0].position[0], 1.0);
            check("v2.x", (*res)[2].position[0], 3.0);
        }
        auto sum = VVDParser::read_checksum(v.span());
        check_i("checksum readable", sum.has_value() ? *sum : 0, kChecksum);
    }

    // The fixup table reorders the array. Source order here is [A, B, C, D]; two runs
    // pull them back as [C, D, A, B]. A parser that ignored fixups would return the
    // original order, which is exactly the scrambled-mesh failure.
    std::printf("VVD with fixups (reordering):\n");
    {
        std::vector<VVDFixup> fixups = {
            {0, 2, 2},   // take C, D
            {0, 0, 2},   // then A, B
        };
        auto v = make_vvd(kChecksum, {{{1, 0, 0}}, {{2, 0, 0}}, {{3, 0, 0}}, {{4, 0, 0}}}, fixups);
        auto res = VVDParser::load_vertices(v.span(), 0);
        if (!res.has_value()) {
            std::printf("  FAIL load_vertices returned an error\n"); ++failures;
        } else {
            check_i("vertex count", (long long)res->size(), 4);
            check("fixup[0] -> C", (*res)[0].position[0], 3.0);
            check("fixup[1] -> D", (*res)[1].position[0], 4.0);
            check("fixup[2] -> A", (*res)[2].position[0], 1.0);
            check("fixup[3] -> B", (*res)[3].position[0], 2.0);
        }
    }

    std::printf("VVD rejects bad input:\n");
    {
        Builder junk;
        junk.put<int32_t>(0, 0xDEADBEEF);
        junk.ensure(64);
        check_i("bad magic rejected", VVDParser::load_vertices(junk.span(), 0).has_value() ? 1 : 0, 0);

        auto v = make_vvd(kChecksum, {{{1, 0, 0}}}, {});
        v.put<int32_t>(4, 3); // wrong version
        check_i("bad version rejected", VVDParser::load_vertices(v.span(), 0).has_value() ? 1 : 0, 0);

        // A fixup run that reaches past the vertex array must be refused, not clamped.
        auto v2 = make_vvd(kChecksum, {{{1, 0, 0}}, {{2, 0, 0}}}, {{0, 1, 99}});
        check_i("overrunning fixup rejected", VVDParser::load_vertices(v2.span(), 0).has_value() ? 1 : 0, 0);
    }

    // ============================================================== full bundle ====
    std::printf("Full bundle (.mdl + .vvd + .vtx):\n");

    // ---- MDL ----
    constexpr size_t kMdlBodyParts = 240;
    constexpr size_t kMdlModel     = 256;
    constexpr size_t kMdlMesh      = 404;
    constexpr size_t kMdlTextures  = 520;
    constexpr size_t kMdlMatName   = 584;

    Builder m;
    m.put_str(0, "IDST", 4);
    m.put<int32_t>(4, 48);                  // version, inside 44..49
    m.put<int32_t>(8, kChecksum);
    m.put_str(12, "props/crate.mdl", 64);
    m.put<int32_t>(156, 0);                 // num_bones (skeleton not under test here)
    m.put<int32_t>(160, 0);
    m.put<int32_t>(204, 1);                 // num_textures
    m.put<int32_t>(208, (int32_t)kMdlTextures);
    m.put<int32_t>(232, 1);                 // num_bodyparts
    m.put<int32_t>(236, (int32_t)kMdlBodyParts);

    // body part -> model  (model_index is relative to the body part struct)
    m.put<int32_t>(kMdlBodyParts + 0, 0);                              // name_index
    m.put<int32_t>(kMdlBodyParts + 4, 1);                              // num_models
    m.put<int32_t>(kMdlBodyParts + 8, 0);                              // base
    m.put<int32_t>(kMdlBodyParts + 12, (int32_t)(kMdlModel - kMdlBodyParts));

    // model -> mesh  (mesh_index is relative to the model struct)
    m.put_str(kMdlModel + 0, "crate", 64);
    m.put<int32_t>(kMdlModel + 72, 1);                                 // num_meshes
    m.put<int32_t>(kMdlModel + 76, (int32_t)(kMdlMesh - kMdlModel));   // mesh_index
    m.put<int32_t>(kMdlModel + 80, 3);                                 // num_vertices
    m.put<int32_t>(kMdlModel + 84, 0);                                 // vertex_index (bytes)

    m.put<int32_t>(kMdlMesh + 0, 0);        // material
    m.put<int32_t>(kMdlMesh + 8, 3);        // num_vertices
    m.put<int32_t>(kMdlMesh + 12, 0);       // vertex_offset

    // texture -> name  (name_index is relative to the texture struct)
    m.put<int32_t>(kMdlTextures + 0, (int32_t)(kMdlMatName - kMdlTextures));
    m.put_str(kMdlMatName, "crate_material", 32);

    // ---- VVD ----
    auto v = make_vvd(kChecksum, {{{10, 20, 30}}, {{40, 50, 60}}, {{70, 80, 90}}}, {});

    // ---- VTX: every offset below is relative to its own struct ----
    constexpr size_t kVtxBodyPart  = 36;
    constexpr size_t kVtxModel     = 44;
    constexpr size_t kVtxLod       = 52;
    constexpr size_t kVtxMesh      = 64;
    constexpr size_t kVtxGroup     = 73;
    constexpr size_t kVtxStripOfs  = 98;
    constexpr size_t kVtxVerts     = 125;
    constexpr size_t kVtxIndices   = 152;

    Builder x;
    x.put<int32_t>(0, kVtxVersion);
    x.put<int32_t>(16, kChecksum);                                  // checksum
    x.put<int32_t>(20, 1);                                          // num_lods
    x.put<int32_t>(28, 1);                                          // num_body_parts
    x.put<int32_t>(32, (int32_t)kVtxBodyPart);                      // body_part_offset

    x.put<int32_t>(kVtxBodyPart + 0, 1);                            // num_models
    x.put<int32_t>(kVtxBodyPart + 4, (int32_t)(kVtxModel - kVtxBodyPart));

    x.put<int32_t>(kVtxModel + 0, 1);                               // num_lods
    x.put<int32_t>(kVtxModel + 4, (int32_t)(kVtxLod - kVtxModel));

    x.put<int32_t>(kVtxLod + 0, 1);                                 // num_meshes
    x.put<int32_t>(kVtxLod + 4, (int32_t)(kVtxMesh - kVtxLod));

    x.put<int32_t>(kVtxMesh + 0, 1);                                // num_strip_groups
    x.put<int32_t>(kVtxMesh + 4, (int32_t)(kVtxGroup - kVtxMesh));
    x.put<uint8_t>(kVtxMesh + 8, 0);                                // flags

    x.put<int32_t>(kVtxGroup + 0, 3);                               // num_verts
    x.put<int32_t>(kVtxGroup + 4, (int32_t)(kVtxVerts - kVtxGroup));
    x.put<int32_t>(kVtxGroup + 8, 3);                               // num_indices
    x.put<int32_t>(kVtxGroup + 12, (int32_t)(kVtxIndices - kVtxGroup));
    x.put<int32_t>(kVtxGroup + 16, 1);                              // num_strips
    x.put<int32_t>(kVtxGroup + 20, (int32_t)(kVtxStripOfs - kVtxGroup));
    x.put<uint8_t>(kVtxGroup + 24, 0);                              // flags

    x.put<int32_t>(kVtxStripOfs + 0, 3);                            // num_indices
    x.put<int32_t>(kVtxStripOfs + 4, 0);                            // index_offset
    x.put<int32_t>(kVtxStripOfs + 8, 3);                            // num_verts
    x.put<int32_t>(kVtxStripOfs + 12, 0);                           // vert_offset
    x.put<int16_t>(kVtxStripOfs + 16, 1);                           // num_bones
    x.put<uint8_t>(kVtxStripOfs + 18, kVtxStripIsTriList);          // flags

    for (int i = 0; i < 3; ++i) {
        const size_t at = kVtxVerts + i * sizeof(VTXVertex);
        x.put<uint8_t>(at + 3, 1);                                  // num_bones
        x.put<uint16_t>(at + 4, (uint16_t)i);                       // orig_mesh_vert_id
    }
    for (int i = 0; i < 3; ++i) {
        x.put<uint16_t>(kVtxIndices + i * 2, (uint16_t)i);
    }

    SourceModelBundle bundle{m.span(), v.span(), x.span()};
    auto res = SourceMDLParser::parse_bundle(bundle);

    if (!res.has_value()) {
        std::printf("  FAIL parse_bundle returned an error\n");
        std::printf("\nFAILURES (%d)\n", failures + 1);
        return 1;
    }

    const auto& mesh = res->mesh_data;
    check_s("model name", mesh.name, "props/crate.mdl");
    check_i("surface count", (long long)mesh.surfaces.size(), 1);

    if (!mesh.surfaces.empty()) {
        const auto& s = mesh.surfaces[0];
        check_s("material name", s.material_name, "crate_material");
        check_i("vertex count", (long long)s.positions.size(), 3);
        check_i("index count", (long long)s.indices.size(), 3);

        if (s.positions.size() == 3) {
            // Source stores model-space vertices, so only the axis remap applies:
            // (x, y, z) -> (x*k, z*k, -y*k)
            check("v0.x", s.positions[0].x,  10.0f * kScale);
            check("v0.y", s.positions[0].y,  30.0f * kScale);
            check("v0.z", s.positions[0].z, -20.0f * kScale);
            check("v2.x", s.positions[2].x,  70.0f * kScale);
            check("v2.y", s.positions[2].y,  90.0f * kScale);
        }
        if (s.uv0.size() == 3) {
            check("uv1.u passthrough", s.uv0[1].x, 1.0);
        }
        if (s.bone_weights.size() == 3) {
            check("v0 weight", s.bone_weights[0][0], 1.0);
        }
    }

    // A .mdl paired with someone else's companions must be refused, not silently
    // decoded into nonsense.
    std::printf("Checksum mismatch:\n");
    {
        auto bad_vvd = make_vvd(0x0BADF00D, {{{1, 0, 0}}, {{2, 0, 0}}, {{3, 0, 0}}}, {});
        SourceModelBundle bad{m.span(), bad_vvd.span(), x.span()};
        auto r = SourceMDLParser::parse_bundle(bad);
        check_i("mismatched .vvd rejected", r.has_value() ? 1 : 0, 0);
    }

    // Without companions the parser must still succeed, just with no geometry.
    std::printf("Skeleton-only fallback:\n");
    {
        auto r = SourceMDLParser::parse(m.span());
        check_i("lone .mdl parses", r.has_value() ? 1 : 0, 1);
        if (r.has_value()) {
            check_i("no surfaces without .vvd/.vtx", (long long)r->mesh_data.surfaces.size(), 0);
        }
    }

    // ================================================== external animation blocks ====
    //
    // studiomdl moves long sequences out of the .mdl entirely: the descriptor then names
    // a block, and the tracks live in a companion .ani at block.data_start + anim_index.
    // Long sequences add a second hop, because frame 0 must be looked up through
    // section 0 rather than through the descriptor. Getting either wrong does not crash,
    // it just silently yields zero poses — which is exactly how m_anm.mdl's 341 stances
    // stayed invisible.
    std::printf("External animation blocks (.ani):\n");
    {
        constexpr size_t kBones    = 664;
        constexpr size_t kBoneName = 1024;
        constexpr size_t kAnims    = 1100;
        constexpr size_t kSeqs     = 1300;
        constexpr size_t kSeqName  = 1600;
        constexpr size_t kBlendIdx = 1640;   // int16 blend grid
        constexpr size_t kBlocks   = 1700;
        constexpr size_t kAniName  = 1800;
        constexpr size_t kSections = 1900;

        Builder a;
        a.put_str(0, "IDST", 4);
        a.put<int32_t>(4, 48);
        a.put<int32_t>(156, 1);                       // num_bones
        a.put<int32_t>(160, (int32_t)kBones);
        a.put<int32_t>(kBones + 0, (int32_t)(kBoneName - kBones)); // name_index
        a.put<int32_t>(kBones + 4, -1);                            // parent
        a.put_str(kBoneName, "ValveBiped.Bip01", 24);

        a.put<int32_t>(180, 1);                       // num_local_anim
        a.put<int32_t>(184, (int32_t)kAnims);
        a.put<int32_t>(188, 1);                       // num_local_seq
        a.put<int32_t>(192, (int32_t)kSeqs);
        a.put<int32_t>(348, (int32_t)kAniName);       // anim_block_name_index
        a.put<int32_t>(352, 2);                       // num_anim_blocks
        a.put<int32_t>(356, (int32_t)kBlocks);
        a.put_str(kAniName, "models/test.ani", 16);

        // Block 0 is reserved to mean "inline"; block 1 is the one under test.
        a.put<int32_t>(kBlocks + 8, 512);             // block 1 data_start
        a.put<int32_t>(kBlocks + 12, 4096);           // block 1 data_end

        a.put<int32_t>(kAnims + 4, (int32_t)(kSeqName - kAnims)); // reuse the label
        a.put<int32_t>(kAnims + 16, 30);              // num_frames
        a.put<int32_t>(kAnims + 52, 1);               // anim_block
        a.put<int32_t>(kAnims + 56, 64);              // anim_index, relative to the block

        a.put<int32_t>(kSeqs + 4, (int32_t)(kSeqName - kSeqs));   // name_index
        a.put<int32_t>(kSeqs + 60, (int32_t)(kBlendIdx - kSeqs)); // anim_index_index
        a.put<int16_t>(kBlendIdx, 0);                             // -> anim descriptor 0
        a.put_str(kSeqName, "idle_smg1", 16);
        a.ensure(2048);

        check_s("anim block name", SourceMDLParser::anim_block_name(a.span()),
                "models/test.ani");

        // The .ani: one bone track carrying a raw Quaternion48 at 512 + 64.
        Builder ani;
        ani.put<uint8_t>(576 + 0, 0);                 // bone
        ani.put<uint8_t>(576 + 1, 0x02);              // kAnimRawRot
        ani.put<int16_t>(576 + 2, 0);                 // next_offset: end of chain
        ani.put<uint16_t>(576 + 4, 32768);            // Quaternion48 x
        ani.put<uint16_t>(576 + 6, 32768);            // y
        ani.put<uint16_t>(576 + 8, 32768);            // z, w-sign clear
        ani.ensure(4096);

        {
            // Without the companion the sequence is unreachable and must be skipped
            // rather than read out of whatever happens to sit at that offset.
            auto r = SourceMDLParser::parse_bundle(SourceModelBundle{a.span(), {}, {}, {}, {}});
            check_i("blocked sequence skipped without .ani",
                    r.has_value() ? (long long)r->skeleton_data.poses.size() : -1, 0);
        }
        {
            auto r = SourceMDLParser::parse_bundle(
                SourceModelBundle{a.span(), {}, {}, ani.span(), {}});
            check_i("blocked sequence recovered with .ani",
                    r.has_value() ? (long long)r->skeleton_data.poses.size() : -1, 1);
            if (r.has_value() && !r->skeleton_data.poses.empty()) {
                check_s("pose name", r->skeleton_data.poses[0].name, "idle_smg1");
                check_i("pose covers every bone",
                        (long long)r->skeleton_data.poses[0].positions.size(), 1);
                check_i("exact lookup beats substring",
                        r->skeleton_data.find_pose_exact("idle_smg1"), 0);
                check_i("exact lookup rejects a near miss",
                        r->skeleton_data.find_pose_exact("idle_smg"), -1);
            }
        }
        {
            // Sectioned: the descriptor's own block/index are decoys, section 0 wins.
            Builder s = a;
            s.put<int32_t>(kAnims + 52, 0);                          // decoy anim_block
            s.put<int32_t>(kAnims + 56, -30260);                     // decoy anim_index
            s.put<int32_t>(kAnims + 80, (int32_t)(kSections - kAnims)); // section_index
            s.put<int32_t>(kAnims + 84, 30);                         // section_frames
            s.put<int32_t>(kSections + 0, 1);                        // section 0 block
            s.put<int32_t>(kSections + 4, 64);                       // section 0 index

            auto r = SourceMDLParser::parse_bundle(
                SourceModelBundle{s.span(), {}, {}, ani.span(), {}});
            check_i("section 0 redirect honoured",
                    r.has_value() ? (long long)r->skeleton_data.poses.size() : -1, 1);
        }
        {
            // A block whose range starts past the end of the .ani must be refused.
            Builder bad = a;
            bad.put<int32_t>(kBlocks + 8, 999999);
            auto r = SourceMDLParser::parse_bundle(
                SourceModelBundle{bad.span(), {}, {}, ani.span(), {}});
            check_i("out-of-range block rejected",
                    r.has_value() ? (long long)r->skeleton_data.poses.size() : -1, 0);
        }
    }

    std::printf("\n%s (%d failure%s)\n", failures == 0 ? "ALL PASS" : "FAILURES",
                failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
