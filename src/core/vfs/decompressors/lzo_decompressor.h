#pragma once

#include <cstddef>
#include <span>

namespace quebratsk::vfs {

/// LZO1X decompressor, the scheme Real Virtuality uses for compressed PAA mipmaps.
///
/// Distinct from the LZSS in this same directory, which is what PBO entries use. The two
/// get confused because both come from Bohemia containers; they share nothing.
///
/// The output size is a parameter rather than something the stream declares, because every
/// caller here already knows it: a DXT payload's size follows from the mipmap's dimensions.
/// That turns the size into a check. A decompressor that goes subtly wrong almost never
/// lands on exactly the right byte count, so demanding the exact size catches errors that
/// a "decompress until it stops" reader would hand back as plausible garbage.
class LZODecompressor {
public:
    /// Decompress `src` into `dst`, which must already be the exact expected size.
    ///
    /// Returns false on malformed input, on any read or write that would leave its buffer,
    /// on a back-reference pointing before the start of the output, and on a stream that
    /// ends without filling `dst` exactly. Nothing outside `dst` is ever written.
    [[nodiscard]] static bool decompress(std::span<const std::byte> src,
                                         std::span<std::byte> dst) noexcept;
};

} // namespace quebratsk::vfs
