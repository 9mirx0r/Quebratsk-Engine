#pragma once

#include "structs/anim_structs.h"

#include <array>
#include <cstddef>
#include <optional>
#include <span>

namespace quebratsk::parsers::source1 {

/// Decoders for Source's compressed animation storage.
///
/// Rotations are quantised into 48 or 64 bits and positions into half-floats, and the
/// per-bone tracks are run-length encoded. These are the primitives that turn that back
/// into plain numbers. Pure arithmetic on plain types — no Godot, no allocation — so it
/// can be unit-tested with no engine present.
class AnimDecoder {
public:
    /// A raw quaternion, in the source (Z-up) frame. Order is x, y, z, w.
    using Quat = std::array<float, 4>;
    using Vec3 = std::array<float, 3>;

    /// IEEE half-precision to float. Source stores positions as three of these.
    [[nodiscard]] static float half_to_float(uint16_t half) noexcept;

    /// Quaternion48: x and y span the full 16 bits, z spans 15, and the remaining bit
    /// carries the sign of w. w itself is not stored — it is recovered from the unit
    /// constraint, which is why a malformed value can push the argument of the square
    /// root negative and has to be clamped.
    [[nodiscard]] static Quat decode_quat48(const SourceQuat48& q) noexcept;

    /// Quaternion64: 21 bits each for x, y and z, plus the sign bit of w.
    [[nodiscard]] static Quat decode_quat64(const SourceQuat64& q) noexcept;

    [[nodiscard]] static Vec3 decode_vec48(const SourceVec48& v) noexcept;

    /// Read one sample from a run-length encoded track.
    ///
    /// A track is a sequence of runs. Each run starts with `{valid, total}`: it covers
    /// `total` frames, of which the first `valid` are stored explicitly and any
    /// remainder repeats the last stored sample. Walking to an arbitrary frame means
    /// stepping run by run, which is why this cannot be a simple array index.
    ///
    /// `track` must start at the first run header. Returns nullopt when the track is
    /// truncated before the requested frame, rather than reading past the end.
    [[nodiscard]] static std::optional<int16_t> sample_track(
        std::span<const SourceAnimValue> track, int32_t frame) noexcept;
};

} // namespace quebratsk::parsers::source1
