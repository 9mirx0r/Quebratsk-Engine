// Verification of the VPK directory reader (src/parsers/source2/vpk2_parser.cpp).
//
// A `_dir.vpk` lists its files as a tree: extension, then folder, then name, each a
// NUL-terminated string, each list ending with an empty string, and an 18-byte entry
// after every name (crc, preload size, archive number, offset, length, 0xFFFF). The
// trees below are built by hand, so where each file must land is known. Exits non-zero
// on failure.
#include "parsers/source2/vpk2_parser.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace quebratsk::parsers::source2;

static int failures = 0;

static void check(const char* what, long long got, long long want) {
    if (got != want) {
        std::printf("  FAIL %-40s got %lld want %lld\n", what, got, want);
        ++failures;
    }
}

static void check_str(const char* what, const std::string& got, const std::string& want) {
    if (got != want) {
        std::printf("  FAIL %-40s got \"%s\" want \"%s\"\n", what, got.c_str(), want.c_str());
        ++failures;
    }
}

template <typename T>
static void put(std::vector<std::byte>& b, T v) {
    const auto* p = reinterpret_cast<const std::byte*>(&v);
    b.insert(b.end(), p, p + sizeof(T));
}

static void str(std::vector<std::byte>& b, const char* s) {
    const auto* p = reinterpret_cast<const std::byte*>(s);
    b.insert(b.end(), p, p + std::strlen(s) + 1);
}

static void entry(std::vector<std::byte>& b, uint32_t crc, uint16_t preload, uint16_t archive,
                  uint32_t offset, uint32_t length, uint16_t terminator = 0xFFFF) {
    put<uint32_t>(b, crc);
    put<uint16_t>(b, preload);
    put<uint16_t>(b, archive);
    put<uint32_t>(b, offset);
    put<uint32_t>(b, length);
    put<uint16_t>(b, terminator);
}

/// The tree used by every case: three files.
///   materials/a/one.vmt  inline, 3 preload bytes, 10 bytes at offset 0 past the tree
///   models/b/two.mdl     in archive 2, 100 bytes at offset 500
///   readme               at the root, no extension, in archive 0
static std::vector<std::byte> tree(uint16_t two_terminator = 0xFFFF) {
    std::vector<std::byte> t;
    str(t, "vmt");
    str(t, "materials/a");
    str(t, "one");
    entry(t, 0x11111111, 3, 0x7FFF, 0, 10);
    str(t, "xy");  // the 3 preload bytes: 'x', 'y', NUL
    str(t, "");    // end of materials/a
    str(t, "");    // end of vmt
    str(t, "mdl");
    str(t, "models/b");
    str(t, "two");
    entry(t, 0x22222222, 0, 2, 500, 100, two_terminator);
    str(t, "");
    str(t, "");
    str(t, " ");   // no extension
    str(t, " ");   // the root folder
    str(t, "readme");
    entry(t, 0x33333333, 0, 0, 0, 7);
    str(t, "");
    str(t, "");
    str(t, "");    // end of the tree
    return t;
}

static std::vector<std::byte> file(uint32_t version, const std::vector<std::byte>& t) {
    std::vector<std::byte> b;
    put<uint32_t>(b, kVpkMagic);
    put<uint32_t>(b, version);
    put<uint32_t>(b, static_cast<uint32_t>(t.size()));
    if (version == 2) for (int i = 0; i < 4; ++i) put<uint32_t>(b, 0);
    b.insert(b.end(), t.begin(), t.end());
    for (int i = 0; i < 10; ++i) b.push_back(std::byte{0});  // the inline file's bytes
    return b;
}

static const VPK2Entry* find(const ParsedVPK2Directory& d, const std::string& path) {
    for (const auto& e : d.entries) if (e.path == path) return &e;
    return nullptr;
}

static void check_tree(const char* name, uint32_t version, size_t header) {
    const auto t = tree();
    const auto bytes = file(version, t);
    const auto res = VPK2Parser::parse_directory(bytes);
    if (!res) {
        std::printf("  FAIL %s refused (error %d)\n", name, static_cast<int>(res.error()));
        ++failures;
        return;
    }
    char what[96];
    auto label = [&](const char* s) {
        std::snprintf(what, sizeof(what), "%s %s", name, s);
        return what;
    };
    const auto tree_end = static_cast<long long>(header + t.size());

    check(label("version"), res->version, version);
    check(label("tree_end"), static_cast<long long>(res->tree_end), tree_end);
    check(label("entries"), static_cast<long long>(res->entries.size()), 3);
    check(label("max_archive_index"), res->max_archive_index, 2);

    const VPK2Entry* one = find(*res, "materials/a/one.vmt");
    const VPK2Entry* two = find(*res, "models/b/two.mdl");
    const VPK2Entry* readme = find(*res, "readme");
    if (!one || !two || !readme) {
        std::printf("  FAIL %s paths:", name);
        for (const auto& e : res->entries) std::printf(" \"%s\"", e.path.c_str());
        std::printf("\n");
        ++failures;
        return;
    }
    check(label("one inline"), one->is_inline(), true);
    check(label("one offset past the tree"), static_cast<long long>(one->offset), tree_end);
    check(label("one length"), static_cast<long long>(one->length), 10);
    check(label("one preload bytes"), one->preload_bytes, 3);
    check_str(label("one preload content"),
              std::string(reinterpret_cast<const char*>(bytes.data() + one->preload_offset), 2),
              "xy");
    check(label("one crc"), one->crc, 0x11111111);
    check(label("two archive"), two->archive_index, 2);
    check(label("two offset"), static_cast<long long>(two->offset), 500);
    check(label("two length"), static_cast<long long>(two->length), 100);
    check(label("readme archive"), readme->archive_index, 0);
}

int main() {
    std::printf("VPK directory verification\n\n");

    check_tree("v1", 1, 12);
    check_tree("v2", 2, 28);

    // ---- refusals ----------------------------------------------------------------
    {
        auto bytes = file(2, tree());
        bytes[0] = std::byte{0};
        check("bad magic refused", VPK2Parser::parse_directory(bytes).has_value(), false);
    }
    {
        auto bytes = file(3, tree());
        check("version 3 refused", VPK2Parser::parse_directory(bytes).has_value(), false);
    }
    {
        // A wrong terminator means the walk has lost its place: everything after it
        // would be invented, so the whole directory is refused.
        auto bytes = file(2, tree(0x1234));
        check("bad terminator refused", VPK2Parser::parse_directory(bytes).has_value(), false);
    }
    {
        // Tree size larger than the file.
        auto bytes = file(1, tree());
        const uint32_t huge = 1u << 20;
        std::memcpy(bytes.data() + 8, &huge, 4);
        check("tree past the end refused", VPK2Parser::parse_directory(bytes).has_value(), false);
    }
    {
        // Cut in the middle of the first entry.
        auto t = tree();
        t.resize(30);
        std::vector<std::byte> bytes;
        put<uint32_t>(bytes, kVpkMagic);
        put<uint32_t>(bytes, 1);
        put<uint32_t>(bytes, static_cast<uint32_t>(t.size()));
        bytes.insert(bytes.end(), t.begin(), t.end());
        check("cut tree refused", VPK2Parser::parse_directory(bytes).has_value(), false);
    }
    {
        const std::vector<std::byte> empty;
        check("empty refused", VPK2Parser::parse_directory(empty).has_value(), false);
    }

    if (failures == 0) std::printf("  all checks passed\n");
    return failures == 0 ? 0 : 1;
}
