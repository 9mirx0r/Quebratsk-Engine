#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace quebratsk::ir {

/// Engine-agnostic decoded image: tightly packed, top-left origin, 8 bits per channel.
///
/// Every texture decoder (VTF, WAD3 miptex, PAA, ...) converges on this type so a
/// single converter can turn it into a Godot ImageTexture. It holds no Godot type,
/// so decoding is safe to run off the main thread.
struct IRTextureData {
    std::string name;
    uint32_t width = 0;
    uint32_t height = 0;
    bool has_alpha = false;
    std::vector<uint8_t> rgba8_pixels; // width * height * 4

    [[nodiscard]] bool is_valid() const {
        if (width == 0 || height == 0) return false;
        const size_t expected = static_cast<size_t>(width) * height * 4;
        return rgba8_pixels.size() == expected;
    }
};

} // namespace quebratsk::ir
