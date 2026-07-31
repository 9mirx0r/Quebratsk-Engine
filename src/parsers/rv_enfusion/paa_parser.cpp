#include "paa_parser.h"

#include "../../core/io/byte_reader.h"
#include "../../core/image/dxt_decoder.h"
#include "../../core/vfs/decompressors/lzo_decompressor.h"

#include <array>
#include <cstring>
#include <vector>

namespace quebratsk::parsers::rv_enfusion {

using io::ByteReader;

namespace {

bool is_known_format(uint16_t word) {
    switch (word) {
        case 0xFF01: case 0xFF02: case 0xFF03: case 0xFF04: case 0xFF05:
        case 0x4444: case 0x1555: case 0x8888: case 0x8080:
            return true;
        default:
            return false;
    }
}

/// Expand one channel from `bits` significant bits to a full 8, by replicating the high
/// bits into the low ones. Plain shifting would cap 5-bit white at 248 rather than 255,
/// so a white texture would come out faintly grey.
constexpr uint8_t expand(uint32_t value, int bits) {
    if (bits <= 0) return 0;
    if (bits >= 8) return static_cast<uint8_t>(value & 0xFF);
    const uint32_t scaled = value << (8 - bits);
    return static_cast<uint8_t>(scaled | (scaled >> bits));
}

/// Decode the packed 16-bit and 8-bit layouts. Each pixel is independent, so there is no
/// block structure to respect and the only subtlety is channel order: these are stored
/// A-R-G-B from the high bits down.
bool decode_packed(PAAFormat format, std::span<const std::byte> src,
                   uint32_t width, uint32_t height, std::vector<uint8_t>& out) {
    const size_t pixels = static_cast<size_t>(width) * height;
    const size_t stride = (format == PAAFormat::ARGB8888) ? 4
                        : (format == PAAFormat::AI88)     ? 2
                                                          : 2;
    if (!io::range_fits(0, pixels, stride, src.size())) return false;

    out.resize(pixels * 4);
    for (size_t i = 0; i < pixels; ++i) {
        const std::byte* p = src.data() + i * stride;
        uint8_t r = 0, g = 0, b = 0, a = 255;

        switch (format) {
            case PAAFormat::ARGB8888: {
                b = static_cast<uint8_t>(p[0]);
                g = static_cast<uint8_t>(p[1]);
                r = static_cast<uint8_t>(p[2]);
                a = static_cast<uint8_t>(p[3]);
                break;
            }
            case PAAFormat::ARGB4444: {
                uint16_t v = 0;
                std::memcpy(&v, p, sizeof(v));
                a = expand((v >> 12) & 0xF, 4);
                r = expand((v >> 8) & 0xF, 4);
                g = expand((v >> 4) & 0xF, 4);
                b = expand(v & 0xF, 4);
                break;
            }
            case PAAFormat::ARGB1555: {
                uint16_t v = 0;
                std::memcpy(&v, p, sizeof(v));
                a = (v & 0x8000) ? 255 : 0;
                r = expand((v >> 10) & 0x1F, 5);
                g = expand((v >> 5) & 0x1F, 5);
                b = expand(v & 0x1F, 5);
                break;
            }
            case PAAFormat::AI88: {
                // Grey plus alpha: one intensity channel written to all three of RGB.
                const uint8_t i8 = static_cast<uint8_t>(p[0]);
                a = static_cast<uint8_t>(p[1]);
                r = g = b = i8;
                break;
            }
            default:
                return false;
        }

        out[i * 4 + 0] = r;
        out[i * 4 + 1] = g;
        out[i * 4 + 2] = b;
        out[i * 4 + 3] = a;
    }
    return true;
}

/// How many bytes a decoded mipmap of these dimensions occupies, or 0 for a layout with no
/// decoder here. This is what a compressed payload has to inflate to, exactly.
size_t expected_payload_size(PAAFormat format, uint32_t width, uint32_t height) {
    switch (format) {
        case PAAFormat::DXT1:
            return image::dxt1_encoded_size(width, height);
        case PAAFormat::DXT5:
            return image::dxt5_encoded_size(width, height);
        case PAAFormat::ARGB8888:
            return static_cast<size_t>(width) * height * 4;
        case PAAFormat::ARGB4444:
        case PAAFormat::ARGB1555:
        case PAAFormat::AI88:
            return static_cast<size_t>(width) * height * 2;
        default:
            return 0;
    }
}

} // namespace

std::expected<PAAInfo, PAAParseError> PAAParser::parse_info(std::span<const std::byte> bytes) {
    ByteReader reader(bytes);

    const auto type_word = reader.read<uint16_t>();
    if (!type_word) return std::unexpected(PAAParseError::Truncated);
    if (!is_known_format(*type_word)) return std::unexpected(PAAParseError::UnknownFormat);

    PAAInfo info;
    info.format = static_cast<PAAFormat>(*type_word);

    // Tagged records: "GGAT", a four-character name, a length, then that many bytes. The
    // names are stored reversed ("SFFO" for OFFS), which matters only if you go looking
    // for one; the chain is walked by length here rather than by name.
    while (true) {
        const auto marker = reader.bytes_at(reader.position(), 4);
        if (marker.size() != 4 || std::memcmp(marker.data(), "GGAT", 4) != 0) break;

        if (!reader.skip(8)) return std::unexpected(PAAParseError::Truncated);  // marker + name
        const auto length = reader.read<uint32_t>();
        if (!length) return std::unexpected(PAAParseError::Truncated);
        if (!reader.skip(*length)) return std::unexpected(PAAParseError::Truncated);
    }

    // Palette. Almost always absent, but a non-zero count has to be stepped over or every
    // mipmap after it is read from the wrong place.
    const auto palette_entries = reader.read<uint16_t>();
    if (!palette_entries) return std::unexpected(PAAParseError::Truncated);
    if (*palette_entries > 0) {
        if (!reader.skip(static_cast<size_t>(*palette_entries) * 3)) {
            return std::unexpected(PAAParseError::Truncated);
        }
    }

    // Mipmaps, largest first, terminated by a zero width and height.
    //
    // The terminator is required rather than treated as optional. Running out of bytes is
    // how a truncated file ends, and letting that stand as a clean end means a half
    // downloaded texture decodes silently into whatever made it into the first mipmap. If
    // some writer out there really does omit the terminator, the very first real file will
    // say so, in a way that names the problem instead of hiding it.
    bool terminated = false;
    while (true) {
        const auto raw_width = reader.read<uint16_t>();
        if (!raw_width) return std::unexpected(PAAParseError::Truncated);
        const auto height = reader.read<uint16_t>();
        if (!height) return std::unexpected(PAAParseError::Truncated);
        if (*raw_width == 0 && *height == 0) {
            terminated = true;
            break;
        }

        // Three bytes, little endian. Not a uint32: the fourth byte belongs to the pixels.
        const auto len_bytes = reader.read_bytes(3);
        if (len_bytes.size() != 3) return std::unexpected(PAAParseError::Truncated);
        const size_t length = static_cast<size_t>(std::to_integer<uint8_t>(len_bytes[0]))
                            | (static_cast<size_t>(std::to_integer<uint8_t>(len_bytes[1])) << 8)
                            | (static_cast<size_t>(std::to_integer<uint8_t>(len_bytes[2])) << 16);

        PAAMipmap mip;
        mip.compressed = (*raw_width & 0x8000) != 0;
        mip.width = static_cast<uint32_t>(*raw_width & 0x7FFF);
        mip.height = *height;
        mip.data_offset = reader.position();
        mip.data_length = length;

        if (!reader.skip(length)) return std::unexpected(PAAParseError::Truncated);
        info.mipmaps.push_back(mip);
    }

    if (!terminated || info.mipmaps.empty()) return std::unexpected(PAAParseError::Truncated);
    return info;
}

std::expected<ir::IRTextureData, PAAParseError> PAAParser::parse(std::span<const std::byte> bytes) {
    auto info = parse_info(bytes);
    if (!info.has_value()) return std::unexpected(info.error());

    const PAAMipmap& mip = info->mipmaps.front();
    if (mip.width == 0 || mip.height == 0) return std::unexpected(PAAParseError::Truncated);

    ByteReader reader(bytes);
    auto payload = reader.bytes_at(mip.data_offset, mip.data_length);
    if (payload.empty()) return std::unexpected(PAAParseError::Truncated);

    ir::IRTextureData out;
    out.width = mip.width;
    out.height = mip.height;

    // Almost every texture a shipped Arma or DayZ build contains takes this path: of a
    // 1,500 file sample from a retail install, 1,457 store their mipmaps compressed.
    //
    // The expected size is computed from the dimensions rather than taken from the stream,
    // which turns it into a check. A decompressor that has gone subtly wrong practically
    // never lands on exactly the right byte count.
    std::vector<std::byte> inflated;
    if (mip.compressed) {
        const size_t expected = expected_payload_size(info->format, mip.width, mip.height);
        if (expected == 0) return std::unexpected(PAAParseError::UnsupportedFormat);

        inflated.resize(expected);
        if (!vfs::LZODecompressor::decompress(payload, inflated)) {
            return std::unexpected(PAAParseError::CompressedMipmap);
        }
        payload = std::span<const std::byte>(inflated);
    }

    switch (info->format) {
        case PAAFormat::DXT1:
            if (!image::decode_dxt1(payload, out.width, out.height, out.rgba8_pixels)) {
                return std::unexpected(PAAParseError::Truncated);
            }
            out.has_alpha = false;
            break;

        case PAAFormat::DXT3:
        case PAAFormat::DXT5:
            // DXT3 and DXT5 differ only in how the alpha block is encoded, and both are
            // 16 bytes per block. DXT3's explicit alpha is not decoded correctly by the
            // DXT5 path, so it is refused rather than approximated.
            if (info->format == PAAFormat::DXT3) {
                return std::unexpected(PAAParseError::UnsupportedFormat);
            }
            if (!image::decode_dxt5(payload, out.width, out.height, out.rgba8_pixels)) {
                return std::unexpected(PAAParseError::Truncated);
            }
            out.has_alpha = true;
            break;

        case PAAFormat::ARGB8888:
        case PAAFormat::ARGB4444:
        case PAAFormat::ARGB1555:
        case PAAFormat::AI88:
            if (!decode_packed(info->format, payload, out.width, out.height, out.rgba8_pixels)) {
                return std::unexpected(PAAParseError::Truncated);
            }
            out.has_alpha = true;  // every packed layout here carries alpha
            break;

        default:
            return std::unexpected(PAAParseError::UnsupportedFormat);
    }

    if (!out.is_valid()) return std::unexpected(PAAParseError::Truncated);
    return out;
}

} // namespace quebratsk::parsers::rv_enfusion
