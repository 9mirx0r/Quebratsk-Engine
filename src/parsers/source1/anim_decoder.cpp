#include "anim_decoder.h"

#include <cmath>
#include <cstring>

namespace quebratsk::parsers::source1 {

float AnimDecoder::half_to_float(uint16_t half) noexcept {
    const uint32_t sign = static_cast<uint32_t>(half >> 15) & 0x1u;
    uint32_t exponent = static_cast<uint32_t>(half >> 10) & 0x1Fu;
    uint32_t mantissa = static_cast<uint32_t>(half) & 0x3FFu;

    if (exponent == 0) {
        if (mantissa == 0) {
            // Signed zero.
            const uint32_t bits = sign << 31;
            float out;
            std::memcpy(&out, &bits, sizeof(out));
            return out;
        }
        // Subnormal: renormalise into the float32 range.
        while ((mantissa & 0x400u) == 0) {
            mantissa <<= 1;
            --exponent;
        }
        ++exponent;
        mantissa &= 0x3FFu;
    } else if (exponent == 0x1F) {
        // Inf or NaN.
        const uint32_t bits = (sign << 31) | 0x7F800000u | (mantissa << 13);
        float out;
        std::memcpy(&out, &bits, sizeof(out));
        return out;
    }

    const uint32_t bits = (sign << 31) |
                          ((exponent + (127 - 15)) << 23) |
                          (mantissa << 13);
    float out;
    std::memcpy(&out, &bits, sizeof(out));
    return out;
}

AnimDecoder::Quat AnimDecoder::decode_quat48(const SourceQuat48& q) noexcept {
    Quat out{};
    out[0] = (static_cast<float>(q.x) - 32768.0f) / 32768.0f;
    out[1] = (static_cast<float>(q.y) - 32768.0f) / 32768.0f;

    const uint16_t z_bits = static_cast<uint16_t>(q.z_and_wneg & 0x7FFFu);
    const bool w_negative = (q.z_and_wneg & 0x8000u) != 0;
    out[2] = (static_cast<float>(z_bits) - 16384.0f) / 16384.0f;

    // w is implied by |q| == 1. Clamp before the root: a corrupt or lossy value can
    // make the sum of squares exceed 1 and produce NaN.
    float w_sq = 1.0f - out[0] * out[0] - out[1] * out[1] - out[2] * out[2];
    if (w_sq < 0.0f) w_sq = 0.0f;
    out[3] = std::sqrt(w_sq);
    if (w_negative) out[3] = -out[3];

    return out;
}

AnimDecoder::Quat AnimDecoder::decode_quat64(const SourceQuat64& q) noexcept {
    // 21 bits each for x, y, z then the sign of w, little-endian across 8 bytes.
    uint64_t bits = 0;
    for (int i = 0; i < 8; ++i) {
        bits |= static_cast<uint64_t>(q.data[i]) << (8 * i);
    }

    const uint32_t xb = static_cast<uint32_t>(bits & 0x1FFFFFu);
    const uint32_t yb = static_cast<uint32_t>((bits >> 21) & 0x1FFFFFu);
    const uint32_t zb = static_cast<uint32_t>((bits >> 42) & 0x1FFFFFu);
    const bool w_negative = ((bits >> 63) & 0x1u) != 0;

    Quat out{};
    out[0] = (static_cast<float>(xb) - 1048576.0f) / 1048576.5f;
    out[1] = (static_cast<float>(yb) - 1048576.0f) / 1048576.5f;
    out[2] = (static_cast<float>(zb) - 1048576.0f) / 1048576.5f;

    float w_sq = 1.0f - out[0] * out[0] - out[1] * out[1] - out[2] * out[2];
    if (w_sq < 0.0f) w_sq = 0.0f;
    out[3] = std::sqrt(w_sq);
    if (w_negative) out[3] = -out[3];

    return out;
}

AnimDecoder::Vec3 AnimDecoder::decode_vec48(const SourceVec48& v) noexcept {
    return {half_to_float(v.x), half_to_float(v.y), half_to_float(v.z)};
}

std::optional<int16_t> AnimDecoder::sample_track(
    std::span<const SourceAnimValue> track, int32_t frame) noexcept {
    if (track.empty() || frame < 0) return std::nullopt;

    size_t cursor = 0;
    int32_t remaining = frame;

    // Step run by run until the run containing `frame` is reached.
    while (true) {
        if (cursor >= track.size()) return std::nullopt;

        const uint8_t valid = track[cursor].num.valid;
        const uint8_t total = track[cursor].num.total;

        // A zero-length run would loop forever.
        if (total == 0) return std::nullopt;

        if (total > remaining) {
            // The sample lives in this run. Frames past the stored ones repeat the last.
            const size_t index = (valid > remaining)
                               ? cursor + 1 + static_cast<size_t>(remaining)
                               : cursor + static_cast<size_t>(valid);
            if (index >= track.size()) return std::nullopt;
            return track[index].value;
        }

        remaining -= total;
        cursor += static_cast<size_t>(valid) + 1;
    }
}

} // namespace quebratsk::parsers::source1
