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

/// Straight channel copy for the uncompressed layouts we support.
void expand_to_rgba8(std::span<const std::byte> src, VTFImageFormat format,
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
            break;
    }
}

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

    // VTF stores mips smallest-first, so the full-resolution level is the LAST one.
    // The old code used header_size as the data offset, which points at the low-res
    // thumbnail. Walk back from the end of the file: the high-res chain is the final
    // block and level 0 is its last entry.
    const size_t frames = header->frames > 0 ? header->frames : 1;
    const size_t hires_total = hires_size * frames;
    if (hires_total > vtf_bytes.size()) {
        return std::unexpected(VTFParseError::CorruptedData);
    }

    const size_t data_offset = vtf_bytes.size() - hires_total;
    if (data_offset < header->header_size) {
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
            // BC2 uses explicit 4-bit alpha rather than BC3's interpolated block.
            return std::unexpected(VTFParseError::UnsupportedFormat);
        default:
            if (payload.size() < image_data_size(format, width, height)) {
                return std::unexpected(VTFParseError::CorruptedData);
            }
            expand_to_rgba8(payload, format, width, height, tex.rgba8_pixels);
            break;
    }

    if (!tex.is_valid()) {
        return std::unexpected(VTFParseError::UnsupportedFormat);
    }

    return tex;
}

} // namespace quebratsk::parsers::source1
