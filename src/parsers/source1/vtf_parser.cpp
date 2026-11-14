#include "vtf_parser.h"
#include "../../core/image/dxt_decoder.h"

#include <algorithm>
#include <cstring>

namespace quebratsk::parsers::source1 {

namespace {

constexpr uint32_t kMaxVtfDimension = 16384;

uint32_t mip_dimension(uint32_t base, uint32_t level) noexcept {
    const uint32_t d = base >> level;
    return d > 0 ? d : 1u;
}

/// Straight channel copy for the uncompressed layouts we support. False for a layout
/// with no copy here, so the caller refuses it instead of handing back a white picture.
bool expand_to_rgba8(std::span<const std::byte> src, VTFImageFormat format,
                     uint32_t width, uint32_t height, std::vector<uint8_t>& out) {
    const size_t pixels = static_cast<size_t>(width) * height;
    out.assign(pixels * 4, 255);

    const auto* s = reinterpret_cast<const uint8_t*>(src.data());

    switch (format) {
        case VTFImageFormat::RGBA8888:
            std::memcpy(out.data(), s, pixels * 4);
            break;
        case VTFImageFormat::BGRA8888:
        case VTFImageFormat::BGRX8888:
            for (size_t i = 0; i < pixels; ++i) {
                out[i * 4 + 0] = s[i * 4 + 2];
                out[i * 4 + 1] = s[i * 4 + 1];
                out[i * 4 + 2] = s[i * 4 + 0];
                out[i * 4 + 3] = (format == VTFImageFormat::BGRX8888) ? 255 : s[i * 4 + 3];
            }
            break;
        case VTFImageFormat::ABGR8888:
            // Alpha first, then the colour backwards. Low-poly packs use it for the small
            // palette textures they paint whole models from.
            for (size_t i = 0; i < pixels; ++i) {
                out[i * 4 + 0] = s[i * 4 + 3];
                out[i * 4 + 1] = s[i * 4 + 2];
                out[i * 4 + 2] = s[i * 4 + 1];
                out[i * 4 + 3] = s[i * 4 + 0];
            }
            break;
        case VTFImageFormat::ARGB8888:
            // Despite the name, the bytes are green, blue, alpha, red: that is where
            // VTFLib, the open reader every VTF tool is built on, finds each channel.
            for (size_t i = 0; i < pixels; ++i) {
                out[i * 4 + 0] = s[i * 4 + 3];
                out[i * 4 + 1] = s[i * 4 + 0];
                out[i * 4 + 2] = s[i * 4 + 1];
                out[i * 4 + 3] = s[i * 4 + 2];
            }
            break;
        case VTFImageFormat::RGB888:
            for (size_t i = 0; i < pixels; ++i) {
                out[i * 4 + 0] = s[i * 3 + 0];
                out[i * 4 + 1] = s[i * 3 + 1];
                out[i * 4 + 2] = s[i * 3 + 2];
                out[i * 4 + 3] = 255;
            }
            break;
        case VTFImageFormat::BGR888:
            for (size_t i = 0; i < pixels; ++i) {
                out[i * 4 + 0] = s[i * 3 + 2];
                out[i * 4 + 1] = s[i * 3 + 1];
                out[i * 4 + 2] = s[i * 3 + 0];
                out[i * 4 + 3] = 255;
            }
            break;
        case VTFImageFormat::I8:
            for (size_t i = 0; i < pixels; ++i) {
                out[i * 4 + 0] = out[i * 4 + 1] = out[i * 4 + 2] = s[i];
                out[i * 4 + 3] = 255;
            }
            break;
        case VTFImageFormat::IA88:
            for (size_t i = 0; i < pixels; ++i) {
                out[i * 4 + 0] = out[i * 4 + 1] = out[i * 4 + 2] = s[i * 2 + 0];
                out[i * 4 + 3] = s[i * 2 + 1];
            }
            break;
        case VTFImageFormat::A8:
            for (size_t i = 0; i < pixels; ++i) {
                out[i * 4 + 0] = out[i * 4 + 1] = out[i * 4 + 2] = 255;
                out[i * 4 + 3] = s[i];
            }
            break;
        default:
            return false;
    }
    return true;
}

template <typename T>
T read_at(std::span<const std::byte> b, size_t offset, T fallback) noexcept {
    if (offset + sizeof(T) > b.size()) return fallback;
    T v;
    std::memcpy(&v, b.data() + offset, sizeof(T));
    return v;
}

// Where the fields past the 63-byte base header sit (packed, as Valve writes them).
constexpr size_t kDepthOffset = 63;         // 7.2+
constexpr size_t kResourceCountOffset = 68; // 7.3+
constexpr size_t kResourcesOffset = 80;     // 7.3+, 8 bytes each: tag[3], flags, offset
constexpr uint32_t kResourceNoData = 0x2;   // the entry's offset field is its value

bool format_has_alpha(VTFImageFormat f) noexcept {
    switch (f) {
        case VTFImageFormat::RGBA8888:
        case VTFImageFormat::ABGR8888:
        case VTFImageFormat::ARGB8888:
        case VTFImageFormat::BGRA8888:
        case VTFImageFormat::IA88:
        case VTFImageFormat::A8:
        case VTFImageFormat::DXT3:
        case VTFImageFormat::DXT5:
        case VTFImageFormat::DXT1_OneBitAlpha:
            return true;
        default:
            return false;
    }
}

} // namespace

size_t VTFParser::image_data_size(VTFImageFormat format, uint32_t width, uint32_t height) noexcept {
    const size_t pixels = static_cast<size_t>(width) * height;
    switch (format) {
        case VTFImageFormat::RGBA8888:
        case VTFImageFormat::ABGR8888:
        case VTFImageFormat::ARGB8888:
        case VTFImageFormat::BGRA8888:
        case VTFImageFormat::BGRX8888:
            return pixels * 4;
        case VTFImageFormat::RGB888:
        case VTFImageFormat::BGR888:
            return pixels * 3;
        case VTFImageFormat::RGB565:
        case VTFImageFormat::IA88:
            return pixels * 2;
        case VTFImageFormat::I8:
        case VTFImageFormat::A8:
        case VTFImageFormat::P8:
            return pixels;
        case VTFImageFormat::DXT1:
        case VTFImageFormat::DXT1_OneBitAlpha:
            return image::dxt1_encoded_size(width, height);
        case VTFImageFormat::DXT3:
        case VTFImageFormat::DXT5:
            return image::dxt5_encoded_size(width, height);
        default:
            return 0;
    }
}

std::expected<ir::IRTextureData, VTFParseError> VTFParser::parse(
    std::span<const std::byte> vtf_bytes
) {
    if (vtf_bytes.size() < sizeof(VTFHeader)) {
        return std::unexpected(VTFParseError::InvalidHeader);
    }

    auto* header = reinterpret_cast<const VTFHeader*>(vtf_bytes.data());
    if (std::memcmp(header->magic, kVtfMagic.data(), 4) != 0) {
        return std::unexpected(VTFParseError::InvalidHeader);
    }

    const uint32_t width = header->width;
    const uint32_t height = header->height;
    if (width == 0 || height == 0 || width > kMaxVtfDimension || height > kMaxVtfDimension) {
        return std::unexpected(VTFParseError::CorruptedData);
    }

    const auto format = static_cast<VTFImageFormat>(header->high_res_image_format);
    const size_t hires_size = image_data_size(format, width, height);
    if (hires_size == 0) {
        // Previously any format fell through to a raw memcpy of width*height*4 bytes,
        // so DXT-compressed textures — the overwhelming majority of Source assets —
        // decoded into noise instead of failing loudly.
        return std::unexpected(VTFParseError::UnsupportedFormat);
    }

    const uint32_t minor = header->version[1];
    const size_t frames = header->frames > 0 ? header->frames : 1;
    const size_t mips = header->mipmap_count > 0 ? header->mipmap_count : 1;
    uint32_t depth = 1;
    if (minor >= 2) {
        depth = read_at<uint16_t>(vtf_bytes, kDepthOffset, 1);
        if (depth == 0) depth = 1;
        if (depth > kMaxVtfDimension) return std::unexpected(VTFParseError::CorruptedData);
    }
    // An environment map is a cube: six faces per frame. Files before 7.5 carry a
    // seventh, a sphere map nobody draws, unless their first frame is -1. Every one of
    // the 96 cube maps in a real 7.4 map has first frame 0 and seven faces, which is
    // also how VTFLib counts them.
    size_t faces = 1;
    if (header->flags & kVtfFlagEnvmap) {
        faces = (minor < 5 && header->first_frame != 0xFFFF) ? 7 : 6;
    }

    // Where the image data begins. 7.3+ says so in its resource list; before that it
    // follows the header and the small thumbnail.
    size_t data_start = 0;
    bool found = false;
    if (minor >= 3) {
        const uint32_t count = read_at<uint32_t>(vtf_bytes, kResourceCountOffset, 0);
        for (uint32_t r = 0; r < count && r < 64; ++r) {
            const size_t at = kResourcesOffset + static_cast<size_t>(r) * 8;
            if (at + 8 > vtf_bytes.size()) break;
            const auto* e = reinterpret_cast<const uint8_t*>(vtf_bytes.data() + at);
            if (e[0] == 0x30 && e[1] == 0 && e[2] == 0 && !(e[3] & kResourceNoData)) {
                data_start = read_at<uint32_t>(vtf_bytes, at + 4, 0);
                found = true;
                break;
            }
        }
    }
    if (!found) {
        data_start = header->header_size;
        const auto thumb = static_cast<VTFImageFormat>(header->low_res_image_format);
        if (header->low_res_image_format != 0xFFFFFFFFu &&
            header->low_res_image_width > 0 && header->low_res_image_height > 0) {
            data_start += image_data_size(thumb, header->low_res_image_width,
                                          header->low_res_image_height);
        }
    }

    // Mips run smallest first, and inside each mip every frame, then every face, then
    // every slice. The full-size image is the first thing of the last mip.
    size_t data_offset = data_start;
    for (size_t level = mips; level-- > 1;) {
        const uint32_t lw = mip_dimension(width, static_cast<uint32_t>(level));
        const uint32_t lh = mip_dimension(height, static_cast<uint32_t>(level));
        const uint32_t ld = mip_dimension(depth, static_cast<uint32_t>(level));
        data_offset += image_data_size(format, lw, lh) * frames * faces * ld;
        if (data_offset > vtf_bytes.size()) return std::unexpected(VTFParseError::CorruptedData);
    }
    if (data_offset < header->header_size || data_offset > vtf_bytes.size() ||
        vtf_bytes.size() - data_offset < hires_size) {
        return std::unexpected(VTFParseError::CorruptedData);
    }

    const auto payload = vtf_bytes.subspan(data_offset, hires_size);

    ir::IRTextureData tex;
    tex.width = width;
    tex.height = height;
    tex.has_alpha = format_has_alpha(format);

    switch (format) {
        case VTFImageFormat::DXT1:
        case VTFImageFormat::DXT1_OneBitAlpha:
            if (!image::decode_dxt1(payload, width, height, tex.rgba8_pixels)) {
                return std::unexpected(VTFParseError::CorruptedData);
            }
            break;
        case VTFImageFormat::DXT5:
            if (!image::decode_dxt5(payload, width, height, tex.rgba8_pixels)) {
                return std::unexpected(VTFParseError::CorruptedData);
            }
            break;
        case VTFImageFormat::DXT3:
            if (!image::decode_dxt3(payload, width, height, tex.rgba8_pixels)) {
                return std::unexpected(VTFParseError::CorruptedData);
            }
            break;
        default:
            if (payload.size() < image_data_size(format, width, height)) {
                return std::unexpected(VTFParseError::CorruptedData);
            }
            if (!expand_to_rgba8(payload, format, width, height, tex.rgba8_pixels)) {
                return std::unexpected(VTFParseError::UnsupportedFormat);
            }
            break;
    }

    if (!tex.is_valid()) {
        return std::unexpected(VTFParseError::UnsupportedFormat);
    }

    return tex;
}

} // namespace quebratsk::parsers::source1
