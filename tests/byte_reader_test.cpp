// Verification of quebratsk::io::ByteReader.
//
// This is the primitive every binary parser now reads through, so its invariant matters
// more than any single parser's: no operation may leave the cursor past the end of the
// buffer, and no read may return data that is not fully inside it.
//
// The adversarial cases below are the ones that actually bit this codebase: offsets and
// counts taken from untrusted files that overflow when added, and NUL-terminated strings
// with no terminator before the end of the mapping.
//
// No Godot, no test framework. Build:
//   cl /nologo /std:c++latest /EHsc /I src tests\byte_reader_test.cpp /Fe:br_test.exe
//   br_test.exe
//
// Exits non-zero on failure.

#include "core/io/byte_reader.h"

#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

using namespace quebratsk::io;

static int failures = 0;

static void ok(const char* what, bool cond) {
    if (!cond) {
        std::printf("  FAIL %s\n", what);
        ++failures;
    }
}
static void eq_i(const char* what, long long got, long long want) {
    if (got != want) {
        std::printf("  FAIL %-42s got %lld want %lld\n", what, got, want);
        ++failures;
    }
}
static void eq_s(const char* what, const std::string& got, const std::string& want) {
    if (got != want) {
        std::printf("  FAIL %-42s got '%s' want '%s'\n", what, got.c_str(), want.c_str());
        ++failures;
    }
}

#pragma pack(push, 1)
struct Packed {
    uint8_t a;
    uint32_t b;   // deliberately misaligned inside the struct
    uint16_t c;
};
#pragma pack(pop)
static_assert(sizeof(Packed) == 7, "packing not applied");

static std::span<const std::byte> as_span(const std::vector<uint8_t>& v) {
    return {reinterpret_cast<const std::byte*>(v.data()), v.size()};
}

int main() {
    std::printf("ByteReader verification\n\n");

    // ------------------------------------------------ range_fits, the core rule ----
    std::printf("range_fits (overflow-safe):\n");
    {
        constexpr size_t max = std::numeric_limits<size_t>::max();

        ok("exact fit accepted",            range_fits(0, 10, 4, 40));
        ok("one past rejected",            !range_fits(0, 11, 4, 40));
        ok("offset at end, zero count",     range_fits(40, 0, 4, 40));
        ok("offset past end rejected",     !range_fits(41, 0, 4, 40));
        ok("zero elem_size, zero count",    range_fits(0, 0, 0, 10));
        ok("zero elem_size, nonzero count",!range_fits(0, 1, 0, 10));

        // The whole point: `offset + count * elem_size` wraps and would report success.
        ok("count*size overflow rejected", !range_fits(0, max / 2 + 1, 4, 1000));
        ok("offset+len overflow rejected", !range_fits(max - 4, 100, 1, 1000));
        ok("huge offset rejected",         !range_fits(max, 1, 1, 1000));
    }

    // --------------------------------------------------------------- basic reads ----
    std::printf("Sequential reads:\n");
    {
        std::vector<uint8_t> buf = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
        ByteReader r(as_span(buf));

        eq_i("initial position", (long long)r.position(), 0);
        eq_i("size",             (long long)r.size(), 8);
        eq_i("remaining",        (long long)r.remaining(), 8);

        auto a = r.read<uint8_t>();
        ok("read<uint8> ok", a.has_value() && *a == 0x01);
        eq_i("advanced by 1", (long long)r.position(), 1);

        auto b = r.read<uint32_t>();
        ok("read<uint32> ok", b.has_value() && *b == 0x05040302u);
        eq_i("advanced by 4", (long long)r.position(), 5);

        eq_i("remaining after", (long long)r.remaining(), 3);
        ok("not at end", !r.at_end());

        // Only 3 bytes left: a uint32 must fail and must NOT move the cursor.
        auto too_big = r.read<uint32_t>();
        ok("short read rejected", !too_big.has_value());
        eq_i("cursor unmoved after failure", (long long)r.position(), 5);
    }

    // ----------------------------------------------------------- packed structs ----
    std::printf("Packed struct reads:\n");
    {
        std::vector<uint8_t> buf(sizeof(Packed) * 2 + 1, 0);
        // Start at offset 1 so the struct is misaligned in the buffer: memcpy must not care.
        Packed p0{0xAA, 0xDEADBEEF, 0x1234};
        Packed p1{0xBB, 0x01020304, 0x5678};
        std::memcpy(buf.data() + 1, &p0, sizeof(Packed));
        std::memcpy(buf.data() + 1 + sizeof(Packed), &p1, sizeof(Packed));

        ByteReader r(as_span(buf), 1);
        auto arr = r.read_array<Packed>(2);
        eq_i("array size", (long long)arr.size(), 2);
        if (arr.size() == 2) {
            eq_i("p0.a", arr[0].a, 0xAA);
            eq_i("p0.b", arr[0].b, 0xDEADBEEF);
            eq_i("p1.c", arr[1].c, 0x5678);
        }
        eq_i("cursor after array", (long long)r.position(), 1 + 2 * sizeof(Packed));

        // One more struct does not fit; must return empty and not move.
        const size_t before = r.position();
        auto none = r.read_array<Packed>(1);
        ok("overrunning array is empty", none.empty());
        eq_i("cursor unmoved", (long long)r.position(), (long long)before);

        // A count that would overflow when multiplied.
        ByteReader r2(as_span(buf));
        auto huge = r2.read_array<Packed>(std::numeric_limits<size_t>::max() / 2);
        ok("overflowing count is empty", huge.empty());
        eq_i("cursor unmoved after overflow", (long long)r2.position(), 0);
    }

    // -------------------------------------------------------------- navigation ----
    std::printf("Navigation:\n");
    {
        std::vector<uint8_t> buf(16, 0);
        ByteReader r(as_span(buf));

        ok("seek to end allowed", r.seek(16));
        ok("at end", r.at_end());
        ok("seek past end rejected", !r.seek(17));
        eq_i("cursor unmoved by bad seek", (long long)r.position(), 16);

        ok("seek back", r.seek(4));
        ok("skip within", r.skip(4));
        eq_i("after skip", (long long)r.position(), 8);
        ok("skip past end rejected", !r.skip(100));
        eq_i("cursor unmoved by bad skip", (long long)r.position(), 8);

        ok("negative signed seek rejected", !r.seek_signed(-1));
        ok("valid signed seek", r.seek_signed(2));
        eq_i("after signed seek", (long long)r.position(), 2);

        // Constructing past the end clamps rather than leaving an invalid cursor.
        ByteReader clamped(as_span(buf), 999);
        eq_i("ctor clamps start", (long long)clamped.position(), 16);
        ok("clamped reader reads nothing", !clamped.read<uint8_t>().has_value());
    }

    // ----------------------------------------------------------------- strings ----
    std::printf("String reads:\n");
    {
        const char* raw = "alpha\0beta\0";
        std::vector<uint8_t> buf(raw, raw + 11);
        ByteReader r(as_span(buf));

        auto s1 = r.read_cstr();
        ok("first cstr", s1.has_value());
        if (s1) eq_s("value", *s1, "alpha");
        eq_i("advanced past NUL", (long long)r.position(), 6);

        auto s2 = r.read_cstr();
        ok("second cstr", s2.has_value());
        if (s2) eq_s("value", *s2, "beta");
        eq_i("advanced past NUL", (long long)r.position(), 11);
    }
    {
        // No terminator before the end: must fail rather than scan past the buffer.
        std::vector<uint8_t> buf = {'n', 'o', 'e', 'n', 'd'};
        ByteReader r(as_span(buf));
        ok("unterminated cstr rejected", !r.read_cstr().has_value());
    }
    {
        // Empty string: a lone NUL is a valid, empty entry (PBO property terminators).
        std::vector<uint8_t> buf = {0x00, 'x', 0x00};
        ByteReader r(as_span(buf));
        auto s = r.read_cstr();
        ok("lone NUL yields empty string", s.has_value() && s->empty());
        eq_i("advanced by 1", (long long)r.position(), 1);
    }
    {
        // Fixed-width NUL-padded field: string stops at NUL, cursor moves the full width.
        std::vector<uint8_t> buf(20, 0);
        std::memcpy(buf.data(), "tex", 3);
        std::memcpy(buf.data() + 16, "\x11\x22\x33\x44", 4);

        ByteReader r(as_span(buf));
        auto name = r.read_fixed_string(16);
        ok("fixed string", name.has_value());
        if (name) eq_s("value", *name, "tex");
        eq_i("advanced full width", (long long)r.position(), 16);

        auto after = r.read<uint32_t>();
        ok("next field aligned correctly", after.has_value() && *after == 0x44332211u);

        // A field wider than the buffer must fail without moving.
        ByteReader r2(as_span(buf));
        ok("oversized fixed field rejected", !r2.read_fixed_string(999).has_value());
        eq_i("cursor unmoved", (long long)r2.position(), 0);
    }
    {
        // An unterminated fixed-width field yields the full width, not a scan past it.
        std::vector<uint8_t> buf(4, 'A');
        ByteReader r(as_span(buf));
        auto s = r.read_fixed_string(4);
        ok("unpadded fixed field", s.has_value());
        if (s) eq_i("length capped at width", (long long)s->size(), 4);
    }

    // -------------------------------------------------------- absolute accessors ----
    std::printf("Absolute accessors:\n");
    {
        std::vector<uint8_t> buf = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
        ByteReader r(as_span(buf));
        (void)r.skip(4);

        auto v = r.read_at<uint16_t>(0);
        ok("read_at value", v.has_value() && *v == 0x2211);
        eq_i("read_at does not move cursor", (long long)r.position(), 4);

        ok("read_at past end rejected", !r.read_at<uint32_t>(6).has_value());

        auto bytes = r.bytes_at(2, 3);
        eq_i("bytes_at size", (long long)bytes.size(), 3);
        ok("bytes_at overrun empty", r.bytes_at(6, 5).empty());

        auto tail = r.tail_at(5);
        eq_i("tail_at size", (long long)tail.size(), 3);
        ok("tail_at past end empty", r.tail_at(99).empty());

        auto sub = r.sub(1, 4);
        eq_i("sub size", (long long)sub.size(), 4);
        auto sv = sub.read<uint8_t>();
        ok("sub reads from its own origin", sv.has_value() && *sv == 0x22);
    }

    // ------------------------------------------------------------- empty buffer ----
    std::printf("Empty buffer:\n");
    {
        ByteReader r{};
        ok("empty", r.empty());
        ok("at end", r.at_end());
        eq_i("remaining", (long long)r.remaining(), 0);
        ok("read fails", !r.read<uint8_t>().has_value());
        ok("cstr fails", !r.read_cstr().has_value());
        ok("array empty", r.read_array<Packed>(1).empty());
        ok("seek(0) ok", r.seek(0));
        ok("skip(0) ok", r.skip(0));
        ok("skip(1) fails", !r.skip(1));
    }

    std::printf("\n%s (%d failure%s)\n", failures == 0 ? "ALL PASS" : "FAILURES",
                failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
