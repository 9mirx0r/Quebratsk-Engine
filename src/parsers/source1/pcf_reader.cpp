#include "pcf_reader.h"

#include <algorithm>
#include <cctype>
#include <unordered_map>

namespace quebratsk::parsers::source1 {

namespace {

bool same_text(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

/// The steps an element array names, each one an operator element with its functionName.
std::vector<PcfOperator> steps(const DmxDocument& doc, const DmxElement& system,
                               std::string_view list) {
    std::vector<PcfOperator> out;
    const DmxAttribute* a = system.find(list);
    if (a == nullptr || a->type != DmxType::Element) return out;
    for (const int32_t e : a->elements) {
        if (e < 0) continue;
        const DmxElement& op = doc.elements[static_cast<size_t>(e)];
        const DmxAttribute* fn = op.find("functionName");
        PcfOperator step;
        // The editor names the element after its function; the attribute is what counts.
        step.function = fn != nullptr && fn->type == DmxType::String ? fn->text() : op.name;
        step.element = e;
        out.push_back(std::move(step));
    }
    return out;
}

} // namespace

int32_t PcfFile::find(std::string_view name) const {
    for (size_t i = 0; i < systems.size(); ++i) {
        if (same_text(systems[i].name, name)) return static_cast<int32_t>(i);
    }
    return -1;
}

PcfFile read_pcf(std::span<const std::byte> data) {
    PcfFile file;
    file.dmx = read_dmx(data);
    if (!file.dmx.valid()) return file;
    if (file.dmx.format != "pcf") {
        file.dmx.error = "format " + file.dmx.format + " is not a particle file";
        return file;
    }
    const auto& elements = file.dmx.elements;

    std::unordered_map<int32_t, int32_t> system_of;   // element -> system
    for (size_t i = 0; i < elements.size(); ++i) {
        if (elements[i].type != "DmeParticleSystemDefinition") continue;
        system_of[static_cast<int32_t>(i)] = static_cast<int32_t>(file.systems.size());
        PcfSystem s;
        s.name = elements[i].name;
        s.element = static_cast<int32_t>(i);
        file.systems.push_back(std::move(s));
    }

    for (auto& s : file.systems) {
        const DmxElement& el = elements[static_cast<size_t>(s.element)];
        s.emitters = steps(file.dmx, el, "emitters");
        s.initializers = steps(file.dmx, el, "initializers");
        s.operators = steps(file.dmx, el, "operators");
        s.renderers = steps(file.dmx, el, "renderers");
        s.forces = steps(file.dmx, el, "forces");
        s.constraints = steps(file.dmx, el, "constraints");

        const DmxAttribute* children = el.find("children");
        if (children == nullptr || children->type != DmxType::Element) continue;
        for (const int32_t c : children->elements) {
            if (c < 0) continue;
            const DmxElement& link = elements[static_cast<size_t>(c)];
            const DmxAttribute* child = link.find("child");
            if (child == nullptr || child->type != DmxType::Element) continue;
            const auto found = system_of.find(child->element());
            if (found == system_of.end()) continue;
            PcfChild out;
            out.system = found->second;
            if (const DmxAttribute* delay = link.find("delay")) {
                out.delay = static_cast<float>(delay->number());
            }
            s.children.push_back(out);
        }
    }
    return file;
}

} // namespace quebratsk::parsers::source1
