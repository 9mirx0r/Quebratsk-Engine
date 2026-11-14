#pragma once

#include "structs/vtf_structs.h"
#include "../../core/ir/ir_texture_data.h"

#include <expected>
#include <span>
#include <vector>

namespace quebratsk::parsers::source1 {

enum class VTFParseError {
    InvalidHeader,
    VersionMismatch,
    UnsupportedFormat,
    CorruptedData,
};

/// Valve Texture Format image formats (VTFImageFormat enum from the SDK).
enum class VTFImageFormat : int32_t {
    None        = -1,
    RGBA8888    = 0,
    ABGR8888    = 1,
    RGB888      = 2,
    BGR888      = 3,
    RGB565      = 4,
    I8          = 5,
    IA88        = 6,
    P8          = 7,
    A8          = 8,
    RGB888_BlueScreen = 9,
    BGR888_BlueScreen = 10,
    ARGB8888    = 11,
    BGRA8888    = 12,
    DXT1        = 13,
    DXT3        = 14,
    DXT5        = 15,
    BGRX8888    = 16,
    BGR565      = 17,
    BGRX5551    = 18,
    BGRA4444    = 19,
    // Was 26 here, which is UVLX8888: real one-bit-alpha textures were refused and a
    // UVLX8888 one would have been decoded as DXT1 noise. 20 is its place in the SDK list.
    DXT1_OneBitAlpha = 20,
    BGRA5551    = 21,
    UV88        = 22,
    UVWQ8888    = 23,
    RGBA16161616F = 24,
    RGBA16161616  = 25,
    UVLX8888    = 26,
};

/// Header flag: the texture is an environment map, stored as a cube of faces.
inline constexpr uint32_t kVtfFlagEnvmap = 0x4000;

class VTFParser {
public:
    /// Decode the highest-resolution mip of a VTF into RGBA8: frame 0, face 0, slice 0.
    [[nodiscard]] static std::expected<ir::IRTextureData, VTFParseError> parse(
        std::span<const std::byte> vtf_bytes
    );

    /// Bytes one mip level of `format` occupies at the given dimensions.
    /// Returns 0 for formats this parser cannot decode.
    [[nodiscard]] static size_t image_data_size(VTFImageFormat format,
                                                uint32_t width, uint32_t height) noexcept;
};

} // namespace quebratsk::parsers::source1
