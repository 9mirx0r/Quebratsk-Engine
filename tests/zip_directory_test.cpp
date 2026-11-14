// Verification of the zip directory reader (src/parsers/source1/zip_directory.cpp).
//
// The zips below are built by hand the way a level compiler packs its pakfile: each file
// stored as it is behind a local header, then the central directory, then the end record.
// So where each file's bytes start is known. Exits non-zero on failure.
#include "parsers/source1/zip_directory.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace quebratsk::parsers::source1;

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

static void raw(std::vector<std::byte>& b, const std::string& s) {
    const auto* p = reinterpret_cast<const std::byte*>(s.data());
    b.insert(b.end(), p, p + s.size());
}

struct File {
    std::string name;
    std::string body;
    std::string local_extra;   // the local header's extra field, which the directory lacks
};

/// A zip of stored files, and where each file's bytes start.
static std::vector<std::byte> make_zip(const std::vector<File>& files,
                                       std::vector<size_t>& starts,
                                       const std::string& comment = "") {
    std::vector<std::byte> z;
    std::vector<size_t> locals;
    for (const auto& f : files) {
        locals.push_back(z.size());
        put<uint32_t>(z, 0x04034b50);
        put<uint16_t>(z, 10);          // version needed
        put<uint16_t>(z, 0);           // flags
        put<uint16_t>(z, 0);           // stored
        put<uint32_t>(z, 0);           // time and date
        put<uint32_t>(z, 0);           // crc
        put<uint32_t>(z, static_cast<uint32_t>(f.body.size()));
        put<uint32_t>(z, static_cast<uint32_t>(f.body.size()));
        put<uint16_t>(z, static_cast<uint16_t>(f.name.size()));
        put<uint16_t>(z, static_cast<uint16_t>(f.local_extra.size()));
        raw(z, f.name);
        raw(z, f.local_extra);
        starts.push_back(z.size());
        raw(z, f.body);
    }
    const size_t dir = z.size();
    for (size_t i = 0; i < files.size(); ++i) {
        const auto& f = files[i];
        put<uint32_t>(z, 0x02014b50);
        put<uint16_t>(z, 20);          // made by
        put<uint16_t>(z, 10);          // needed
        put<uint16_t>(z, 0);
        put<uint16_t>(z, 0);           // stored
        put<uint32_t>(z, 0);
        put<uint32_t>(z, 0);
        put<uint32_t>(z, static_cast<uint32_t>(f.body.size()));
        put<uint32_t>(z, static_cast<uint32_t>(f.body.size()));
        put<uint16_t>(z, static_cast<uint16_t>(f.name.size()));
        put<uint16_t>(z, 0);           // extra
        put<uint16_t>(z, 0);           // comment
        put<uint16_t>(z, 0);           // disk
        put<uint16_t>(z, 0);           // internal attributes
        put<uint32_t>(z, 0);           // external attributes
        put<uint32_t>(z, static_cast<uint32_t>(locals[i]));
        raw(z, f.name);
    }
    const size_t dir_size = z.size() - dir;
    put<uint32_t>(z, 0x06054b50);
    put<uint16_t>(z, 0);
    put<uint16_t>(z, 0);
    put<uint16_t>(z, static_cast<uint16_t>(files.size()));
    put<uint16_t>(z, static_cast<uint16_t>(files.size()));
    put<uint32_t>(z, static_cast<uint32_t>(dir_size));
    put<uint32_t>(z, static_cast<uint32_t>(dir));
    put<uint16_t>(z, static_cast<uint16_t>(comment.size()));
    raw(z, comment);
    return z;
}

static std::string body_of(const std::vector<std::byte>& z, const ZipEntry& e) {
    return std::string(reinterpret_cast<const char*>(z.data() + e.offset), e.stored_size);
}

static void test_stored_files() {
    std::vector<size_t> starts;
    const auto z = make_zip({{"materials/maps/x/c0_0_0.vmt", "\"patch\" {}", ""},
                             {"materials/maps/x/", "", ""},
                             {"maps/x_cubemaps.vtf", std::string("VTF\0pixels", 10),
                              std::string("\x01\x00\x04\x00" "abcd", 8)}},
                            starts, "made by hand");
    const auto list = read_zip_directory(z);
    check("files listed (folder left out)", static_cast<long long>(list.size()), 2);
    if (list.size() != 2) return;
    check_str("first name", list[0].name, "materials/maps/x/c0_0_0.vmt");
    check("first starts", static_cast<long long>(list[0].offset), static_cast<long long>(starts[0]));
    check_str("first body", body_of(z, list[0]), "\"patch\" {}");
    check("first is stored", list[0].is_stored(), 1);
    // The local header carries an extra field the directory does not; the bytes start
    // after the local one.
    check("second starts", static_cast<long long>(list[1].offset), static_cast<long long>(starts[2]));
    check_str("second body", body_of(z, list[1]), std::string("VTF\0pixels", 10));
}

static void test_level_pakfile() {
    std::vector<size_t> starts;
    const auto zip = make_zip({{"materials/a.vmt", "abc", ""}}, starts);

    std::vector<std::byte> level;
    raw(level, "VBSP");
    put<int32_t>(level, 20);
    const size_t table = level.size();
    level.resize(table + 64 * 16, std::byte{0});
    const size_t at = level.size();
    level.insert(level.end(), zip.begin(), zip.end());
    const uint32_t off = static_cast<uint32_t>(at);
    const uint32_t len = static_cast<uint32_t>(zip.size());
    std::memcpy(level.data() + table + 40 * 16, &off, 4);
    std::memcpy(level.data() + table + 40 * 16 + 4, &len, 4);

    const auto pak = vbsp_pakfile(level);
    check("pakfile found", static_cast<long long>(pak.size()), static_cast<long long>(zip.size()));
    const auto list = read_zip_directory(pak);
    check("pakfile files", static_cast<long long>(list.size()), 1);
    if (list.size() == 1) check_str("pakfile body", body_of(zip, list[0]), "abc");

    std::vector<std::byte> goldsrc(2048, std::byte{0});
    goldsrc[0] = std::byte{30};
    check("a GoldSrc level has none", static_cast<long long>(vbsp_pakfile(goldsrc).size()), 0);
}

static void test_broken() {
    std::vector<size_t> starts;
    auto z = make_zip({{"a.vmt", "abc", ""}, {"b.vmt", "defg", ""}}, starts);
    check("not a zip", static_cast<long long>(read_zip_directory(std::span<const std::byte>(z.data(), 10)).size()), 0);

    // The directory says the second file starts past the end.
    auto cut = z;
    const size_t dir = cut.size() - 22 - 2 * (46 + 5);
    const uint32_t far = 0x7FFFFFFF;
    std::memcpy(cut.data() + dir + 46 + 5 + 42, &far, 4);
    const auto list = read_zip_directory(cut);
    check("bad entry skipped, good one kept", static_cast<long long>(list.size()), 1);
}

int main() {
    test_stored_files();
    test_level_pakfile();
    test_broken();
    if (failures == 0) std::printf("zip_directory_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
