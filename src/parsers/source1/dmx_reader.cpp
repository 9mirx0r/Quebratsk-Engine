#include "dmx_reader.h"

#include "core/io/byte_reader.h"

#include <cstdio>
#include <optional>

namespace quebratsk::parsers::source1 {

namespace {

using io::ByteReader;

constexpr uint8_t kArrayOffset = 14;   // AT_ELEMENT_ARRAY - AT_ELEMENT

/// The string table and the rules for using it, which change with the encoding version.
struct Strings {
    int version = 0;
    std::vector<std::string> table;

    /// A name from the table (version 2 and up), or written out in place (version 1).
    [[nodiscard]] std::optional<std::string> name(ByteReader& r) const {
        if (version < 2) return r.read_cstr();
        return lookup(r);
    }

    /// A value from the table: element names and single string values from version 4 on,
    /// written out in place before that.
    [[nodiscard]] std::optional<std::string> value(ByteReader& r) const {
        if (version < 4) return r.read_cstr();
        return lookup(r);
    }

    [[nodiscard]] std::optional<std::string> lookup(ByteReader& r) const {
        int64_t index = 0;
        if (version >= 5) {
            const auto i = r.read<int32_t>();
            if (!i) return std::nullopt;
            index = *i;
        } else {
            const auto i = r.read<int16_t>();
            if (!i) return std::nullopt;
            // Written as a short and counted as one, so the table can hold up to 65535.
            index = static_cast<uint16_t>(*i);
        }
        if (index < 0 || static_cast<size_t>(index) >= table.size()) return std::nullopt;
        return table[static_cast<size_t>(index)];
    }
};

bool read_numbers(ByteReader& r, DmxType type, std::vector<double>& out) {
    switch (type) {
    case DmxType::Int: {
        const auto v = r.read<int32_t>();
        if (!v) return false;
        out.push_back(*v);
        return true;
    }
    case DmxType::Bool: {
        const auto v = r.read<uint8_t>();
        if (!v) return false;
        out.push_back(*v != 0 ? 1.0 : 0.0);
        return true;
    }
    case DmxType::Time: {
        // Kept as tenths of a millisecond.
        const auto v = r.read<int32_t>();
        if (!v) return false;
        out.push_back(*v / 10000.0);
        return true;
    }
    case DmxType::Color: {
        for (int i = 0; i < 4; ++i) {
            const auto v = r.read<uint8_t>();
            if (!v) return false;
            out.push_back(*v);
        }
        return true;
    }
    default: {
        const size_t n = dmx_components(type);
        if (n == 0) return false;
        for (size_t i = 0; i < n; ++i) {
            const auto v = r.read<float>();
            if (!v) return false;
            out.push_back(*v);
        }
        return true;
    }
    }
}

/// One value of `type`. `in_array` because a string inside an array is always written out
/// in place, whatever the version.
bool read_value(ByteReader& r, const Strings& strings, DmxType type, bool in_array,
                DmxAttribute& out) {
    switch (type) {
    case DmxType::Element: {
        const auto index = r.read<int32_t>();
        if (!index) return false;
        if (*index == -2) {
            // Kept elsewhere: its id follows as text.
            if (!r.read_cstr()) return false;
            out.elements.push_back(-1);
        } else {
            out.elements.push_back(*index < 0 ? -1 : *index);
        }
        return true;
    }
    case DmxType::String: {
        const auto s = in_array ? r.read_cstr() : strings.value(r);
        if (!s) return false;
        out.texts.push_back(*s);
        return true;
    }
    case DmxType::Void: {
        const auto n = r.read<int32_t>();
        return n && *n >= 0 && r.skip(static_cast<size_t>(*n));
    }
    case DmxType::Time:
        if (strings.version < 3) return r.skip(16);   // the object id it replaced
        return read_numbers(r, type, out.numbers);
    default:
        return read_numbers(r, type, out.numbers);
    }
}

bool parse_header(const std::string& line, DmxDocument& doc) {
    char encoding[64] = {};
    char format[64] = {};
    int ev = 0;
    int fv = 0;
    if (std::sscanf(line.c_str(), "<!-- dmx encoding %63s %d format %63s %d", encoding, &ev,
                    format, &fv) != 4) {
        return false;
    }
    doc.encoding = encoding;
    doc.encoding_version = ev;
    doc.format = format;
    doc.format_version = fv;
    return true;
}

} // namespace

size_t dmx_components(DmxType type) {
    switch (type) {
    case DmxType::Int:
    case DmxType::Float:
    case DmxType::Bool:
    case DmxType::Time:
        return 1;
    case DmxType::Vector2:
        return 2;
    case DmxType::Vector3:
    case DmxType::QAngle:
        return 3;
    case DmxType::Color:
    case DmxType::Vector4:
    case DmxType::Quaternion:
        return 4;
    case DmxType::Matrix:
        return 16;
    default:
        return 0;
    }
}

const DmxAttribute* DmxElement::find(std::string_view wanted) const {
    for (const auto& a : attributes) {
        if (a.name == wanted) return &a;
    }
    return nullptr;
}

DmxDocument read_dmx(std::span<const std::byte> data) {
    DmxDocument doc;
    ByteReader r(data);
    const auto header = r.read_cstr();
    if (!header || !parse_header(*header, doc)) {
        doc.error = "not a DMX header";
        return doc;
    }
    if (doc.encoding != "binary") {
        doc.error = "encoding " + doc.encoding + " is not read";
        return doc;
    }
    if (doc.encoding_version < 1 || doc.encoding_version > 5) {
        doc.error = "binary encoding " + std::to_string(doc.encoding_version) + " is not known";
        return doc;
    }

    Strings strings;
    strings.version = doc.encoding_version;
    if (strings.version >= 2) {
        int64_t count = 0;
        if (strings.version >= 4) {
            const auto n = r.read<int32_t>();
            if (!n) { doc.error = "cut short in the string table"; return doc; }
            count = *n;
        } else {
            const auto n = r.read<int16_t>();
            if (!n) { doc.error = "cut short in the string table"; return doc; }
            count = static_cast<uint16_t>(*n);
        }
        // Each string is at least its terminator, so a count past the bytes left is a lie.
        if (count < 0 || static_cast<size_t>(count) > r.remaining()) {
            doc.error = "string table larger than the file";
            return doc;
        }
        strings.table.reserve(static_cast<size_t>(count));
        for (int64_t i = 0; i < count; ++i) {
            auto s = r.read_cstr();
            if (!s) { doc.error = "cut short in the string table"; return doc; }
            strings.table.push_back(std::move(*s));
        }
    }

    const auto count = r.read<int32_t>();
    // An element is at least a type index and its 16-byte id.
    if (!count || *count < 0 || static_cast<size_t>(*count) > r.remaining() / 18) {
        doc.error = "element count larger than the file";
        return doc;
    }
    doc.elements.resize(static_cast<size_t>(*count));
    for (auto& el : doc.elements) {
        const auto type = strings.name(r);
        const auto name = strings.value(r);
        const auto id = r.read_bytes(16);
        if (!type || !name || id.empty()) {
            doc.error = "cut short in the element list";
            doc.elements.clear();
            return doc;
        }
        el.type = *type;
        el.name = *name;
        for (size_t i = 0; i < 16; ++i) el.id[i] = static_cast<uint8_t>(id[i]);
    }

    const auto total = static_cast<int32_t>(doc.elements.size());
    for (auto& el : doc.elements) {
        const auto n = r.read<int32_t>();
        // An attribute is at least a name index and a type byte.
        if (!n || *n < 0 || static_cast<size_t>(*n) > r.remaining() / 3) {
            doc.error = "cut short in the attributes of " + el.name;
            doc.elements.clear();
            return doc;
        }
        el.attributes.resize(static_cast<size_t>(*n));
        for (auto& a : el.attributes) {
            const auto name = strings.name(r);
            const auto raw = r.read<uint8_t>();
            if (!name || !raw) {
                doc.error = "cut short in the attributes of " + el.name;
                doc.elements.clear();
                return doc;
            }
            a.name = *name;
            const bool array = *raw > kArrayOffset;
            const uint8_t base = array ? static_cast<uint8_t>(*raw - kArrayOffset) : *raw;
            if (base < 1 || base > static_cast<uint8_t>(DmxType::Matrix)) {
                doc.error = "attribute " + a.name + " has unknown type " + std::to_string(*raw);
                doc.elements.clear();
                return doc;
            }
            a.type = static_cast<DmxType>(base);
            a.array = array;
            size_t values = 1;
            if (array) {
                const auto c = r.read<int32_t>();
                if (!c || *c < 0 || static_cast<size_t>(*c) > r.remaining()) {
                    doc.error = "cut short in " + a.name;
                    doc.elements.clear();
                    return doc;
                }
                values = static_cast<size_t>(*c);
            }
            a.count = values;
            for (size_t v = 0; v < values; ++v) {
                if (!read_value(r, strings, a.type, array, a)) {
                    doc.error = "cut short in " + a.name;
                    doc.elements.clear();
                    return doc;
                }
            }
            // A link past the end of the list points nowhere.
            for (auto& e : a.elements) {
                if (e >= total) e = -1;
            }
        }
    }
    doc.end = r.position();
    return doc;
}

} // namespace quebratsk::parsers::source1
