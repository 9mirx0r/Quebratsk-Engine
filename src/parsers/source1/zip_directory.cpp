#include "zip_directory.h"

#include <cstring>

namespace quebratsk::parsers::source1 {

namespace {

// The zip layout used here is the one in PKWARE's published APPNOTE: a local header before
// each file's bytes, a central directory listing them all, and an end record pointing at it.
constexpr uint32_t kLocalHeader = 0x04034b50;
constexpr uint32_t kCentralHeader = 0x02014b50;
constexpr uint32_t kEndRecord = 0x06054b50;
constexpr size_t kLocalHeaderSize = 30;
constexpr size_t kCentralHeaderSize = 46;
constexpr size_t kEndRecordSize = 22;
constexpr size_t kMaxComment = 0xFFFF;

bool fits(size_t at, size_t len, size_t size) {
    return at <= size && len <= size - at;
}

uint16_t u16(std::span<const std::byte> b, size_t at) {
    uint16_t v = 0;
    std::memcpy(&v, b.data() + at, sizeof v);
    return v;
}

uint32_t u32(std::span<const std::byte> b, size_t at) {
    uint32_t v = 0;
    std::memcpy(&v, b.data() + at, sizeof v);
    return v;
}

} // namespace

std::vector<ZipEntry> read_zip_directory(std::span<const std::byte> zip) {
    std::vector<ZipEntry> out;
    if (zip.size() < kEndRecordSize) return out;

    // The end record is the last thing in the zip, followed only by a comment of up to
    // 64 KB, so it is looked for from the back.
    size_t end = SIZE_MAX;
    const size_t last = zip.size() - kEndRecordSize;
    const size_t first = last > kMaxComment ? last - kMaxComment : 0;
    for (size_t at = last + 1; at-- > first;) {
        if (u32(zip, at) == kEndRecord) {
            end = at;
            break;
        }
    }
    if (end == SIZE_MAX) return out;

    const uint16_t count = u16(zip, end + 10);
    const size_t dir_size = u32(zip, end + 12);
    size_t at = u32(zip, end + 16);
    if (!fits(at, dir_size, zip.size())) return out;

    out.reserve(count);
    for (uint16_t i = 0; i < count; ++i) {
        if (!fits(at, kCentralHeaderSize, zip.size()) || u32(zip, at) != kCentralHeader) break;
        const uint16_t method = u16(zip, at + 10);
        const size_t stored = u32(zip, at + 20);
        const size_t size = u32(zip, at + 24);
        const size_t name_len = u16(zip, at + 28);
        const size_t extra_len = u16(zip, at + 30);
        const size_t comment_len = u16(zip, at + 32);
        const size_t local = u32(zip, at + 42);
        if (!fits(at + kCentralHeaderSize, name_len, zip.size())) break;

        std::string name(reinterpret_cast<const char*>(zip.data() + at + kCentralHeaderSize),
                         name_len);
        at += kCentralHeaderSize + name_len + extra_len + comment_len;

        if (name.empty() || name.back() == '/') continue;   // a folder

        // The bytes start after the file's own local header, whose name and extra field
        // need not be the same length as the directory's copy.
        if (!fits(local, kLocalHeaderSize, zip.size()) || u32(zip, local) != kLocalHeader) {
            continue;
        }
        const size_t data = local + kLocalHeaderSize + u16(zip, local + 26) + u16(zip, local + 28);
        if (!fits(data, stored, zip.size())) continue;

        ZipEntry entry;
        entry.name = std::move(name);
        entry.method = method;
        entry.offset = data;
        entry.stored_size = stored;
        entry.size = size;
        out.push_back(std::move(entry));
    }
    return out;
}

std::span<const std::byte> vbsp_pakfile(std::span<const std::byte> level) {
    // "VBSP", the version, then 64 lump records of offset, length, version and fourCC.
    constexpr size_t kLumpTable = 8;
    constexpr size_t kLumpRecord = 16;
    constexpr size_t kPakfileLump = 40;
    const size_t record = kLumpTable + kPakfileLump * kLumpRecord;
    if (!fits(0, record + kLumpRecord, level.size())) return {};
    if (std::memcmp(level.data(), "VBSP", 4) != 0) return {};

    const size_t offset = u32(level, record);
    const size_t length = u32(level, record + 4);
    if (length == 0 || !fits(offset, length, level.size())) return {};
    return level.subspan(offset, length);
}

} // namespace quebratsk::parsers::source1
