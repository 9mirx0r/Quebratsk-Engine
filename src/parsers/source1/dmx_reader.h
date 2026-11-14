#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace quebratsk::parsers::source1 {

/// What one attribute holds, as the binary DMX numbers them (`sdk`
/// public/datamodel/dmattributetypes.h). An array is the same number plus 14.
enum class DmxType : uint8_t {
    Unknown = 0,
    Element = 1,
    Int,
    Float,
    Bool,
    String,
    Void,       // a block of bytes
    Time,       // before encoding 3 this slot was an object id instead
    Color,      // four bytes, r g b a
    Vector2,
    Vector3,
    Vector4,
    QAngle,
    Quaternion,
    Matrix,     // 4 x 4 floats
};

/// How many numbers one value of `type` is made of; 0 for those that are not numbers.
[[nodiscard]] size_t dmx_components(DmxType type);

/// One named value of an element. Numbers of every kind go into `numbers`, one value after
/// another, so a vector3 array of 2 is six numbers; strings into `texts`; links to other
/// elements into `elements`, as their place in the document, or -1 for none (or one left
/// out of the file).
struct DmxAttribute {
    std::string name;
    DmxType type = DmxType::Unknown;
    bool array = false;
    size_t count = 0;     // values held: 1 for a single one
    std::vector<double> numbers;
    std::vector<std::string> texts;
    std::vector<int32_t> elements;

    /// The `i`th number, or `fallback` when there is none.
    [[nodiscard]] double number(size_t i = 0, double fallback = 0.0) const {
        return i < numbers.size() ? numbers[i] : fallback;
    }
    [[nodiscard]] std::string text(size_t i = 0) const {
        return i < texts.size() ? texts[i] : std::string();
    }
    [[nodiscard]] int32_t element(size_t i = 0) const {
        return i < elements.size() ? elements[i] : -1;
    }
};

/// One element: a type name, its own name, a 16-byte id, and its attributes in file order.
struct DmxElement {
    std::string type;
    std::string name;
    std::array<uint8_t, 16> id{};
    std::vector<DmxAttribute> attributes;

    /// The attribute called `name`, or null.
    [[nodiscard]] const DmxAttribute* find(std::string_view name) const;
};

/// A whole binary DMX: what its header says, then every element. The first is the root.
struct DmxDocument {
    std::string encoding;       // "binary"
    int encoding_version = 0;   // 1 to 5
    std::string format;         // "pcf", "model", ...
    int format_version = 0;
    std::vector<DmxElement> elements;
    std::string error;          // why it was not read, when it was not
    size_t end = 0;             // how far into the file the reading got

    [[nodiscard]] bool valid() const { return error.empty() && !elements.empty(); }
};

/// Read a binary DMX (the Valve Developer Wiki's DMX/Binary page, encodings 1 to 5).
///
/// The text encoding (keyvalues2) and the older "binary_v2" header are not read: the
/// document comes back with `error` set. So does one that is cut short anywhere, rather than
/// half read: a particle file with half its operators would draw something wrong.
[[nodiscard]] DmxDocument read_dmx(std::span<const std::byte> data);

} // namespace quebratsk::parsers::source1
