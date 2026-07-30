#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace quebratsk::image {

/// S3TC / DXT block decoders.
///
/// Pure functions over raw bytes: no Godot types, no allocation beyond the output
/// buffer, safe to call from any thread. Sizes are computed in size_t throughout so
/// large dimensions cannot overflow the way `width * height * 4` did in uint32.
///
/// Both formats encode 4x4 pixel blocks, so an image is padded up to a multiple of 4
/// on disk; the decoders write only the pixels inside the declared width/height.

/// Bytes a DXT1 payload occupies for the given dimensions (8 bytes per 4x4 block).
[[nodiscard]] size_t dxt1_encoded_size(uint32_t width, uint32_t height) noexcept;

/// Bytes a DXT3/DXT5 payload occupies for the given dimensions (16 bytes per block).
[[nodiscard]] size_t dxt5_encoded_size(uint32_t width, uint32_t height) noexcept;

/// Decode DXT1 (BC1) into tightly packed RGBA8. Returns false if `src` is too small.
/// DXT1 carries 1-bit alpha: when the first endpoint is not greater than the second,
/// index 3 means fully transparent black.
[[nodiscard]] bool decode_dxt1(std::span<const std::byte> src,
                               uint32_t width, uint32_t height,
                               std::vector<uint8_t>& out_rgba8);

/// Decode DXT5 (BC3) into tightly packed RGBA8. Returns false if `src` is too small.
[[nodiscard]] bool decode_dxt5(std::span<const std::byte> src,
                               uint32_t width, uint32_t height,
                               std::vector<uint8_t>& out_rgba8);

} // namespace quebratsk::image
