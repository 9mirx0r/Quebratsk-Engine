#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <type_traits>

namespace quebratsk::io {

/// Does `count` elements of `elem_size` bytes fit at `offset` within `total` bytes?
///
/// Written with subtraction and division so it cannot overflow. The obvious form,
/// `offset + count * elem_size <= total`, wraps around size_t for values taken from an
/// untrusted file and then reports success — which is exactly how several of the
/// out-of-bounds reads in this codebase's audit came about.
[[nodiscard]] constexpr bool range_fits(size_t offset, size_t count, size_t elem_size,
                                        size_t total) noexcept {
    if (offset > total) return false;
    if (elem_size == 0) return count == 0;
    return count <= (total - offset) / elem_size;
}

/// Bounds-checked forward cursor over an immutable byte span.
///
/// The invariant is simple and total: **no operation can leave the cursor past the end
/// of the buffer, and no read can return data that is not fully inside it.** Every read
/// either succeeds or reports failure; there is no partial-read path and no way to
/// advance past the end by accident.
///
/// This type exists because the same hand-rolled "check then memcpy then advance"
/// sequence was repeated across every binary parser here, in four subtly different
/// variants. Four of the twelve critical findings in the engineering audit were
/// unchecked reads or overflowing offset arithmetic in that pattern.
///
/// Holds no Godot type and allocates only for the string readers, so it is safe to use
/// from any thread and can be unit-tested with no engine present.
///
/// NOTE on `read_array` / `peek_struct`: these hand back pointers into the buffer for
/// `#pragma pack(1)` structs read from a memory-mapped file, which is the established
/// idiom for these formats. The caller must only use trivially-copyable packed types.
class ByteReader {
public:
    ByteReader() = default;

    explicit ByteReader(std::span<const std::byte> data, size_t start = 0) noexcept
        : _data(data), _pos(start <= data.size() ? start : data.size()) {}

    // ---------------------------------------------------------------- queries ----

    [[nodiscard]] size_t position() const noexcept { return _pos; }
    [[nodiscard]] size_t size() const noexcept { return _data.size(); }
    [[nodiscard]] size_t remaining() const noexcept { return _data.size() - _pos; }
    [[nodiscard]] bool at_end() const noexcept { return _pos >= _data.size(); }
    [[nodiscard]] bool empty() const noexcept { return _data.empty(); }

    [[nodiscard]] bool can_read(size_t bytes) const noexcept {
        return range_fits(_pos, bytes, 1, _data.size());
    }

    template <typename T>
    [[nodiscard]] bool can_read_array(size_t count) const noexcept {
        return range_fits(_pos, count, sizeof(T), _data.size());
    }

    // ------------------------------------------------------------- navigation ----

    /// Move to an absolute offset. Fails (and leaves the cursor untouched) if the
    /// offset is past the end, so a bad offset from a file cannot desynchronise the
    /// reader.
    [[nodiscard]] bool seek(size_t absolute) noexcept {
        if (absolute > _data.size()) return false;
        _pos = absolute;
        return true;
    }

    /// Signed seek for formats whose offsets are int32 on disk; negative is rejected.
    [[nodiscard]] bool seek_signed(int64_t absolute) noexcept {
        if (absolute < 0) return false;
        return seek(static_cast<size_t>(absolute));
    }

    [[nodiscard]] bool skip(size_t bytes) noexcept {
        if (!can_read(bytes)) return false;
        _pos += bytes;
        return true;
    }

    // ------------------------------------------------------------------ reads ----

    /// Read one trivially-copyable value and advance. memcpy rather than a cast, so
    /// alignment of the source bytes is irrelevant.
    template <typename T>
    [[nodiscard]] std::optional<T> read() noexcept {
        static_assert(std::is_trivially_copyable_v<T>, "ByteReader::read needs a POD type");
        if (!can_read(sizeof(T))) return std::nullopt;
        T out;
        std::memcpy(&out, _data.data() + _pos, sizeof(T));
        _pos += sizeof(T);
        return out;
    }

    /// Read one value at an absolute offset without moving the cursor.
    template <typename T>
    [[nodiscard]] std::optional<T> read_at(size_t absolute) const noexcept {
        static_assert(std::is_trivially_copyable_v<T>, "ByteReader::read_at needs a POD type");
        if (!range_fits(absolute, 1, sizeof(T), _data.size())) return std::nullopt;
        T out;
        std::memcpy(&out, _data.data() + absolute, sizeof(T));
        return out;
    }

    /// Zero-copy view of `count` packed structs, advancing the cursor.
    /// Returns an empty span if they do not all fit — check `.empty()`, never the size
    /// you asked for.
    template <typename T>
    [[nodiscard]] std::span<const T> read_array(size_t count) noexcept {
        static_assert(std::is_trivially_copyable_v<T>, "ByteReader::read_array needs a POD type");
        if (!can_read_array<T>(count)) return {};
        const auto* p = reinterpret_cast<const T*>(_data.data() + _pos);
        _pos += count * sizeof(T);
        return {p, count};
    }

    /// Zero-copy view of `count` packed structs at an absolute offset, cursor unmoved.
    template <typename T>
    [[nodiscard]] std::span<const T> array_at(size_t absolute, size_t count) const noexcept {
        static_assert(std::is_trivially_copyable_v<T>, "ByteReader::array_at needs a POD type");
        if (!range_fits(absolute, count, sizeof(T), _data.size())) return {};
        const auto* p = reinterpret_cast<const T*>(_data.data() + absolute);
        return {p, count};
    }

    /// Raw bytes, advancing. Empty span on failure.
    [[nodiscard]] std::span<const std::byte> read_bytes(size_t count) noexcept {
        if (!can_read(count)) return {};
        const auto out = _data.subspan(_pos, count);
        _pos += count;
        return out;
    }

    /// Raw bytes at an absolute offset, cursor unmoved. Empty span on failure.
    [[nodiscard]] std::span<const std::byte> bytes_at(size_t absolute, size_t count) const noexcept {
        if (!range_fits(absolute, count, 1, _data.size())) return {};
        return _data.subspan(absolute, count);
    }

    /// Everything from an absolute offset to the end. Empty if the offset is past it.
    [[nodiscard]] std::span<const std::byte> tail_at(size_t absolute) const noexcept {
        if (absolute > _data.size()) return {};
        return _data.subspan(absolute);
    }

    /// NUL-terminated string, advancing past the terminator.
    ///
    /// Returns nullopt when no terminator exists before the end of the buffer, which
    /// means the container is truncated. Constructing a std::string from a raw pointer
    /// instead would scan past the end of the mapping — a real defect that appeared
    /// twice in this codebase.
    [[nodiscard]] std::optional<std::string> read_cstr() noexcept {
        const size_t start = _pos;
        size_t scan = _pos;
        while (scan < _data.size() && static_cast<char>(_data[scan]) != '\0') {
            ++scan;
        }
        if (scan >= _data.size()) return std::nullopt; // no terminator inside the buffer
        std::string out(reinterpret_cast<const char*>(_data.data() + start), scan - start);
        _pos = scan + 1;
        return out;
    }

    /// NUL-terminated string at an absolute offset, cursor unmoved.
    [[nodiscard]] std::optional<std::string> cstr_at(size_t absolute) const noexcept {
        if (absolute >= _data.size()) return std::nullopt;
        size_t scan = absolute;
        while (scan < _data.size() && static_cast<char>(_data[scan]) != '\0') {
            ++scan;
        }
        if (scan >= _data.size()) return std::nullopt;
        return std::string(reinterpret_cast<const char*>(_data.data() + absolute), scan - absolute);
    }

    /// Fixed-width NUL-padded field (`char name[16]` and friends), advancing the full
    /// width. The string stops at the first NUL but the cursor always moves `width`.
    [[nodiscard]] std::optional<std::string> read_fixed_string(size_t width) noexcept {
        if (!can_read(width)) return std::nullopt;
        const auto* p = reinterpret_cast<const char*>(_data.data() + _pos);
        const size_t len = ::strnlen(p, width);
        std::string out(p, len);
        _pos += width;
        return out;
    }

    /// A sub-reader over an absolute range, for nested structures.
    [[nodiscard]] ByteReader sub(size_t absolute, size_t count) const noexcept {
        return ByteReader(bytes_at(absolute, count));
    }

private:
    std::span<const std::byte> _data;
    size_t _pos = 0;
};

} // namespace quebratsk::io
