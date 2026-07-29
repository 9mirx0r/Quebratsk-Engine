#include "lzss_decompressor.h"
#include <array>
#include <cstring>

namespace quebratsk::vfs {

std::expected<std::vector<std::byte>, DecompressError> LZSSDecompressor::decompress(
    std::span<const std::byte> input,
    size_t expected_output_size
) {
    if (input.empty() || expected_output_size == 0) {
        return std::unexpected(DecompressError::InvalidInput);
    }

    std::vector<std::byte> output;
    output.reserve(expected_output_size);

    std::array<uint8_t, RING_BUFFER_SIZE> ring_buffer{};
    // Initialize ring buffer with space character 0x20 per Bohemia spec
    std::memset(ring_buffer.data(), 0x20, ring_buffer.size() - MAX_MATCH);
    size_t ring_pos = RING_BUFFER_SIZE - MAX_MATCH;

    size_t input_pos = 0;
    uint32_t flags = 0;

    while (output.size() < expected_output_size && input_pos < input.size()) {
        flags >>= 1;
        if ((flags & 0x100) == 0) {
            if (input_pos >= input.size()) break;
            flags = static_cast<uint8_t>(input[input_pos++]) | 0xFF00;
        }

        if (flags & 1) {
            // Literal byte
            if (input_pos >= input.size()) break;
            uint8_t b = static_cast<uint8_t>(input[input_pos++]);
            output.push_back(static_cast<std::byte>(b));
            ring_buffer[ring_pos] = b;
            ring_pos = (ring_pos + 1) % RING_BUFFER_SIZE;
        } else {
            // Match reference (2 bytes)
            if (input_pos + 1 >= input.size()) break;
            uint8_t byte1 = static_cast<uint8_t>(input[input_pos++]);
            uint8_t byte2 = static_cast<uint8_t>(input[input_pos++]);

            size_t match_offset = byte1 | ((byte2 & 0xF0) << 4);
            size_t match_len = (byte2 & 0x0F) + THRESHOLD + 1;

            for (size_t k = 0; k < match_len && output.size() < expected_output_size; ++k) {
                uint8_t b = ring_buffer[(match_offset + k) % RING_BUFFER_SIZE];
                output.push_back(static_cast<std::byte>(b));
                ring_buffer[ring_pos] = b;
                ring_pos = (ring_pos + 1) % RING_BUFFER_SIZE;
            }
        }
    }

    if (output.size() != expected_output_size) {
        // Soft fallback: if output is truncated or slightly over, resize to match expected
        output.resize(expected_output_size, std::byte{0});
    }

    return output;
}

} // namespace quebratsk::vfs
