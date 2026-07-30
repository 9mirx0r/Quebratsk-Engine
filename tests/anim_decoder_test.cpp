// Verification of the Source animation decoders.
//
// Rotations are quantised into 48 or 64 bits with w reconstructed from the unit
// constraint, positions are half-floats, and the per-bone tracks are run-length
// encoded. All three are places where an off-by-one or a sign error produces a pose
// that is subtly wrong rather than obviously broken, so they are pinned here.
//
// No Godot, no test framework. Build:
//   cl /nologo /std:c++latest /EHsc /I src tests\anim_decoder_test.cpp ^
//      src\parsers\source1\anim_decoder.cpp /Fe:anim_test.exe
//   anim_test.exe

#include "parsers/source1/anim_decoder.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace quebratsk::parsers::source1;

static int failures = 0;

static void close(const char* what, double got, double want, double tol = 1e-4) {
    if (std::fabs(got - want) > tol) {
        std::printf("  FAIL %-40s got %12.6f want %12.6f\n", what, got, want);
        ++failures;
    }
}
static void ok(const char* what, bool cond) {
    if (!cond) { std::printf("  FAIL %s\n", what); ++failures; }
}

int main() {
    std::printf("Source animation decoder verification\n\n");

    // ------------------------------------------------------------- half floats ----
    std::printf("half_to_float:\n");
    {
        close("+0",        AnimDecoder::half_to_float(0x0000), 0.0);
        close("-0",        AnimDecoder::half_to_float(0x8000), 0.0);
        close("1.0",       AnimDecoder::half_to_float(0x3C00), 1.0);
        close("-1.0",      AnimDecoder::half_to_float(0xBC00), -1.0);
        close("2.0",       AnimDecoder::half_to_float(0x4000), 2.0);
        close("0.5",       AnimDecoder::half_to_float(0x3800), 0.5);
        close("-2.5",      AnimDecoder::half_to_float(0xC100), -2.5);
        close("65504 max", AnimDecoder::half_to_float(0x7BFF), 65504.0, 1.0);
        // Smallest subnormal: 2^-24. Renormalisation is its own code path.
        close("subnormal", AnimDecoder::half_to_float(0x0001), 5.9604645e-8, 1e-10);
        ok("+inf", std::isinf(AnimDecoder::half_to_float(0x7C00)));
        ok("nan",  std::isnan(AnimDecoder::half_to_float(0x7E00)));
    }

    // ------------------------------------------------------------ Quaternion48 ----
    std::printf("decode_quat48:\n");
    {
        // Midpoint of every field is zero; w then reconstructs to 1 (identity).
        SourceQuat48 q{};
        q.x = 32768;
        q.y = 32768;
        q.z_and_wneg = 16384;         // z midpoint, w positive
        auto r = AnimDecoder::decode_quat48(q);
        close("identity.x", r[0], 0.0);
        close("identity.y", r[1], 0.0);
        close("identity.z", r[2], 0.0);
        close("identity.w", r[3], 1.0);

        // Same, but the top bit of the z field flags w as negative.
        q.z_and_wneg = 16384 | 0x8000;
        r = AnimDecoder::decode_quat48(q);
        close("w sign bit honoured", r[3], -1.0);
        close("z unaffected by sign bit", r[2], 0.0);

        // x at full scale: w must collapse to 0, not go NaN.
        q.x = 65535;
        q.y = 32768;
        q.z_and_wneg = 16384;
        r = AnimDecoder::decode_quat48(q);
        close("x near +1", r[0], 0.99997, 1e-3);
        ok("w stays finite", std::isfinite(r[3]));
        close("w collapses to 0", r[3], 0.0, 1e-2);

        // Result must be unit length for a well-formed value.
        q.x = 45000; q.y = 32768; q.z_and_wneg = 16384;
        r = AnimDecoder::decode_quat48(q);
        const double len = std::sqrt(r[0]*r[0] + r[1]*r[1] + r[2]*r[2] + r[3]*r[3]);
        close("unit length", len, 1.0, 1e-3);
    }

    // ------------------------------------------------------------ Quaternion64 ----
    std::printf("decode_quat64:\n");
    {
        // x = y = z = 1048576 (midpoint) -> zero; w reconstructs to 1.
        const uint64_t mid = 1048576ull;
        const uint64_t bits = mid | (mid << 21) | (mid << 42);
        SourceQuat64 q{};
        for (int i = 0; i < 8; ++i) q.data[i] = static_cast<uint8_t>((bits >> (8 * i)) & 0xFF);

        auto r = AnimDecoder::decode_quat64(q);
        close("identity.x", r[0], 0.0, 1e-5);
        close("identity.y", r[1], 0.0, 1e-5);
        close("identity.z", r[2], 0.0, 1e-5);
        close("identity.w", r[3], 1.0, 1e-4);

        // Top bit is the sign of w and must not bleed into z.
        const uint64_t neg = bits | (1ull << 63);
        for (int i = 0; i < 8; ++i) q.data[i] = static_cast<uint8_t>((neg >> (8 * i)) & 0xFF);
        r = AnimDecoder::decode_quat64(q);
        close("w sign bit honoured", r[3], -1.0, 1e-4);
        close("z unaffected by sign bit", r[2], 0.0, 1e-5);
    }

    // ---------------------------------------------------------------- Vector48 ----
    std::printf("decode_vec48:\n");
    {
        SourceVec48 v{0x3C00, 0xC000, 0x0000}; // 1.0, -2.0, 0.0
        auto r = AnimDecoder::decode_vec48(v);
        close("x", r[0], 1.0);
        close("y", r[1], -2.0);
        close("z", r[2], 0.0);
    }

    // ------------------------------------------------------------- RLE sampling ----
    std::printf("sample_track (run-length encoded):\n");
    {
        // One run: 3 stored samples covering 5 frames. Frames 3 and 4 repeat sample 2.
        std::vector<SourceAnimValue> track(4);
        track[0].num.valid = 3;
        track[0].num.total = 5;
        track[1].value = 100;
        track[2].value = 200;
        track[3].value = 300;

        auto v0 = AnimDecoder::sample_track(track, 0);
        auto v1 = AnimDecoder::sample_track(track, 1);
        auto v2 = AnimDecoder::sample_track(track, 2);
        auto v3 = AnimDecoder::sample_track(track, 3);
        auto v4 = AnimDecoder::sample_track(track, 4);

        ok("frame 0", v0.has_value() && *v0 == 100);
        ok("frame 1", v1.has_value() && *v1 == 200);
        ok("frame 2", v2.has_value() && *v2 == 300);
        ok("frame 3 repeats last", v3.has_value() && *v3 == 300);
        ok("frame 4 repeats last", v4.has_value() && *v4 == 300);

        // Past the end of the run: truncated, not a wild read.
        ok("frame 5 rejected", !AnimDecoder::sample_track(track, 5).has_value());
        ok("negative frame rejected", !AnimDecoder::sample_track(track, -1).has_value());
    }
    {
        // Two runs: the walk has to skip the first before indexing the second.
        std::vector<SourceAnimValue> track(6);
        track[0].num.valid = 2; track[0].num.total = 2;
        track[1].value = 10;
        track[2].value = 20;
        track[3].num.valid = 2; track[3].num.total = 2;
        track[4].value = 30;
        track[5].value = 40;

        auto a = AnimDecoder::sample_track(track, 0);
        auto b = AnimDecoder::sample_track(track, 1);
        auto c = AnimDecoder::sample_track(track, 2);
        auto d = AnimDecoder::sample_track(track, 3);

        ok("run 1 frame 0", a.has_value() && *a == 10);
        ok("run 1 frame 1", b.has_value() && *b == 20);
        ok("run 2 frame 0", c.has_value() && *c == 30);
        ok("run 2 frame 1", d.has_value() && *d == 40);
        ok("past both runs rejected", !AnimDecoder::sample_track(track, 4).has_value());
    }
    {
        // A run declaring total == 0 would never advance; it must be refused.
        std::vector<SourceAnimValue> track(2);
        track[0].num.valid = 1;
        track[0].num.total = 0;
        track[1].value = 7;
        ok("zero-length run rejected", !AnimDecoder::sample_track(track, 0).has_value());

        ok("empty track rejected", !AnimDecoder::sample_track({}, 0).has_value());
    }
    {
        // Header promises more samples than the buffer holds.
        std::vector<SourceAnimValue> track(2);
        track[0].num.valid = 9;
        track[0].num.total = 9;
        track[1].value = 5;
        auto r = AnimDecoder::sample_track(track, 4);
        ok("truncated run rejected", !r.has_value());
    }

    std::printf("\n%s (%d failure%s)\n", failures == 0 ? "ALL PASS" : "FAILURES",
                failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
