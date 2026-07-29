#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>
#include <expected>

namespace quebratsk::vfs {

enum class DecompressError {
    InvalidInput,
    BufferOverflow,
    CorruptedData,
};

/// Bohemia Interactive LZSS Decompressor (used in PBO "Cprs" CPRS entries)
/// 4096-byte ring buffer, 18-byte max match length, 2-byte threshold.
class LZSSDecompressor {
public:
    static constexpr size_t RING_BUFFER_SIZE = 4096;
    static constexpr size_t MAX_MATCH = 18;
    static constexpr size_t THRESHOLD = 2;

    /// Decompress LZSS input stream into output buffer
    [[nodiscard]] static std::expected<std::vector<std::byte>, DecompressError> decompress(
        std::span<const std::byte> input,
        size_t expected_output_size
    );
};

} // namespace quebratsk::vfs
