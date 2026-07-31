#include "lzo_decompressor.h"

#include <cstdint>

namespace quebratsk::vfs {

namespace {

/// The furthest back an M2 match can reach, which the format fixes at 2 KiB.
constexpr size_t kM2MaxOffset = 0x0800;

/// Read the variable-length run extension: a run of zero bytes each meaning 255, then a
/// final non-zero byte. Advances `ip`. Returns false if the stream ends inside it.
bool read_run_length(const uint8_t*& ip, const uint8_t* ip_end, size_t& t, size_t base) {
    while (true) {
        if (ip >= ip_end) return false;
        if (*ip != 0) break;
        // A stream of nothing but zeroes would spin here forever on a corrupt file.
        if (t > (1u << 30)) return false;
        t += 255;
        ++ip;
    }
    t += base + *ip++;
    return true;
}

} // namespace

// The control flow below is the reference LZO1X decoder's, gotos included. It was kept in
// that shape deliberately: the algorithm's states genuinely jump into each other's middles,
// and every attempt to restructure it into loops introduces a subtly different decoder that
// still decompresses most inputs. What has been added is the bounds checking the reference
// leaves to its caller, on every read, every write and every back-reference.
bool LZODecompressor::decompress(std::span<const std::byte> src,
                                 std::span<std::byte> dst) noexcept {
    if (src.empty() || dst.empty()) return false;

    const uint8_t* ip = reinterpret_cast<const uint8_t*>(src.data());
    const uint8_t* const ip_end = ip + src.size();
    uint8_t* const op_begin = reinterpret_cast<uint8_t*>(dst.data());
    uint8_t* op = op_begin;
    uint8_t* const op_end = op_begin + dst.size();

    auto in_avail = [&](size_t n) { return static_cast<size_t>(ip_end - ip) >= n; };
    auto out_avail = [&](size_t n) { return static_cast<size_t>(op_end - op) >= n; };

    // Copy `n` literal bytes from the input.
    auto copy_literals = [&](size_t n) {
        if (!in_avail(n) || !out_avail(n)) return false;
        for (size_t i = 0; i < n; ++i) *op++ = *ip++;
        return true;
    };

    // Copy `n` bytes from earlier in the output. Byte at a time on purpose: the source and
    // destination overlap by design, which is how a short match expands into a long run.
    auto copy_match = [&](const uint8_t* m_pos, size_t n) {
        if (m_pos < op_begin || m_pos >= op) return false;
        if (!out_avail(n)) return false;
        for (size_t i = 0; i < n; ++i) *op++ = *m_pos++;
        return true;
    };

    size_t t = 0;
    const uint8_t* m_pos = nullptr;

    if (*ip > 17) {
        t = static_cast<size_t>(*ip++) - 17;
        if (t < 4) goto match_next;
        if (!copy_literals(t)) return false;
        goto first_literal_run;
    }

    for (;;) {
        if (!in_avail(1)) return false;
        t = *ip++;
        if (t >= 16) goto match;

        if (t == 0) {
            if (!read_run_length(ip, ip_end, t, 15)) return false;
        }
        if (!copy_literals(t + 3)) return false;

    first_literal_run:
        if (!in_avail(1)) return false;
        t = *ip++;
        if (t >= 16) goto match;

        if (!in_avail(1)) return false;
        {
            const size_t back = 1 + kM2MaxOffset + (t >> 2) + (static_cast<size_t>(*ip++) << 2);
            if (back > static_cast<size_t>(op - op_begin)) return false;
            m_pos = op - back;
        }
        if (!copy_match(m_pos, 3)) return false;
        goto match_done;

        for (;;) {
        match:
            if (t >= 64) {
                if (!in_avail(1)) return false;
                const size_t back = 1 + ((t >> 2) & 7) + (static_cast<size_t>(*ip++) << 3);
                if (back > static_cast<size_t>(op - op_begin)) return false;
                m_pos = op - back;
                t = (t >> 5) - 1;
            } else if (t >= 32) {
                t &= 31;
                if (t == 0) {
                    if (!read_run_length(ip, ip_end, t, 31)) return false;
                }
                if (!in_avail(2)) return false;
                {
                    const size_t back = 1 + ((static_cast<size_t>(ip[0]) >> 2)
                                             + (static_cast<size_t>(ip[1]) << 6));
                    ip += 2;
                    if (back > static_cast<size_t>(op - op_begin)) return false;
                    m_pos = op - back;
                }
            } else if (t >= 16) {
                size_t back = (static_cast<size_t>(t & 8) << 11);
                t &= 7;
                if (t == 0) {
                    if (!read_run_length(ip, ip_end, t, 7)) return false;
                }
                if (!in_avail(2)) return false;
                {
                    const size_t extra = (static_cast<size_t>(ip[0]) >> 2)
                                       + (static_cast<size_t>(ip[1]) << 6);
                    ip += 2;
                    // The one sentinel in the format: this exact encoding, resolving to the
                    // current output position, is the end of the stream rather than a match.
                    if (back == 0 && extra == 0) goto eof_found;
                    back += extra + 0x4000;
                    if (back > static_cast<size_t>(op - op_begin)) return false;
                    m_pos = op - back;
                }
            } else {
                if (!in_avail(1)) return false;
                const size_t back = 1 + (t >> 2) + (static_cast<size_t>(*ip++) << 2);
                if (back > static_cast<size_t>(op - op_begin)) return false;
                m_pos = op - back;
                if (!copy_match(m_pos, 2)) return false;
                goto match_done;
            }

            if (!copy_match(m_pos, t + 2)) return false;

        match_done:
            // The two low bits of the second-to-last consumed byte carry a short literal
            // run, which is how the encoder packs one to three trailing bytes without
            // spending a whole control byte on them.
            if (ip - 2 < reinterpret_cast<const uint8_t*>(src.data())) return false;
            t = static_cast<size_t>(ip[-2]) & 3;
            if (t == 0) break;

        match_next:
            if (!copy_literals(t)) return false;
            if (!in_avail(1)) return false;
            t = *ip++;
        }
    }

eof_found:
    // Exactly full, or the stream did not describe the data it claimed to.
    return op == op_end;
}

} // namespace quebratsk::vfs
