#include "vtf_parser.h"
#include <cstring>

namespace quebratsk::parsers::source1 {

std::expected<DecodedVTFImage, VTFParseError> VTFParser::parse(
    std::span<const std::byte> vtf_bytes
) {
    if (vtf_bytes.size() < sizeof(VTFHeader)) {
        return std::unexpected(VTFParseError::InvalidHeader);
    }

    auto* header = reinterpret_cast<const VTFHeader*>(vtf_bytes.data());
    if (std::memcmp(header->magic, kVtfMagic.data(), 4) != 0) {
        return std::unexpected(VTFParseError::InvalidHeader);
    }

    DecodedVTFImage img;
    img.width = header->width;
    img.height = header->height;
    img.format = header->high_res_image_format;

    size_t num_pixels = static_cast<size_t>(img.width) * static_cast<size_t>(img.height);
    if (num_pixels == 0) {
        return std::unexpected(VTFParseError::CorruptedData);
    }

    size_t data_offset = header->header_size;
    if (data_offset >= vtf_bytes.size()) {
        return std::unexpected(VTFParseError::CorruptedData);
    }

    // Default allocation for RGBA8 output
    img.rgba8_pixels.resize(num_pixels * 4, 255);

    // If payload contains uncompressed RGBA8888 or RGB888
    size_t payload_bytes = vtf_bytes.size() - data_offset;
    if (payload_bytes >= num_pixels * 4) {
        std::memcpy(img.rgba8_pixels.data(), vtf_bytes.data() + data_offset, num_pixels * 4);
    }

    return img;
}

} // namespace quebratsk::parsers::source1
