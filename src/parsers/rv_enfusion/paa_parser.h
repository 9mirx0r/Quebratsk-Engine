#pragma once

#include "../../core/ir/ir_texture_data.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>

namespace quebratsk::parsers::rv_enfusion {

enum class PAAParseError {
    /// The leading word is not one of the known pixel layouts. Legacy untyped PAAs and
    /// files that are not PAAs at all both land here.
    UnknownFormat,
    /// Header, tag chain or mipmap table runs off the end of the buffer.
    Truncated,
    /// The file parsed, but its largest mipmap is stored compressed and this build cannot
    /// decompress it yet. Distinct from Truncated on purpose: it means "the file is fine,
    /// this reader is not", which is a different thing to tell a user.
    CompressedMipmap,
    /// A pixel layout that is recognised but has no decoder here.
    UnsupportedFormat,
};

/// The pixel layout named by a PAA's leading word.
enum class PAAFormat : uint16_t {
    DXT1 = 0xFF01,
    DXT2 = 0xFF02,
    DXT3 = 0xFF03,
    DXT4 = 0xFF04,
    DXT5 = 0xFF05,
    ARGB4444 = 0x4444,
    ARGB1555 = 0x1555,
    ARGB8888 = 0x8888,
    AI88 = 0x8080,
};

/// One entry of the mipmap chain, located but not yet decoded.
struct PAAMipmap {
    uint32_t width = 0;
    uint32_t height = 0;
    bool compressed = false;  // the width field carried the high bit
    size_t data_offset = 0;
    size_t data_length = 0;
};

/// What a PAA declares about itself, without decoding any pixels.
struct PAAInfo {
    PAAFormat format = PAAFormat::DXT1;
    std::vector<PAAMipmap> mipmaps;  // largest first, which is the order on disk
};

/// Real Virtuality texture container (Arma, DayZ).
///
/// A PAA is a pixel-format word, a chain of tagged records, an optional palette, and then
/// a mipmap chain from largest to smallest. The pixel payload is usually DXT, which this
/// project already decodes, so the work here is the container rather than the compression.
///
/// Each mipmap says separately whether its bytes are compressed, by setting the high bit of
/// its width. Those are not readable yet and are reported as such rather than decoded into
/// noise.
class PAAParser {
public:
    /// Read the header and locate every mipmap, decoding nothing.
    /// Cheap enough to call to find out what a file is before committing to reading it.
    [[nodiscard]] static std::expected<PAAInfo, PAAParseError> parse_info(
        std::span<const std::byte> bytes);

    /// Decode the largest mipmap into RGBA8.
    [[nodiscard]] static std::expected<ir::IRTextureData, PAAParseError> parse(
        std::span<const std::byte> bytes);
};

} // namespace quebratsk::parsers::rv_enfusion
