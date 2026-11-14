#pragma once

#include "dmx_reader.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace quebratsk::parsers::source1 {

/// One step of a particle system: what the particle editor calls an emitter, initializer,
/// operator or renderer. `function` says which ("emit_instantaneously", "Radius Random");
/// its settings are the attributes of `element`.
struct PcfOperator {
    std::string function;
    int32_t element = -1;
};

/// Another system started alongside this one, `delay` seconds later.
struct PcfChild {
    int32_t system = -1;   // into PcfFile::systems
    float delay = 0.0f;
};

/// One particle system, as the particle editor builds it: its own settings (material, how
/// many at most, starting size and colour) on `element`, then the steps in their four kinds,
/// plus forces and constraints, then the systems it brings along.
struct PcfSystem {
    std::string name;
    int32_t element = -1;
    std::vector<PcfOperator> emitters;
    std::vector<PcfOperator> initializers;
    std::vector<PcfOperator> operators;
    std::vector<PcfOperator> renderers;
    std::vector<PcfOperator> forces;
    std::vector<PcfOperator> constraints;
    std::vector<PcfChild> children;
};

/// A particle file: the document it was read from, and the systems in it.
struct PcfFile {
    DmxDocument dmx;
    std::vector<PcfSystem> systems;

    [[nodiscard]] bool valid() const { return dmx.valid() && !systems.empty(); }

    /// The system called `name` (as the game asks for it, in any case), or -1.
    [[nodiscard]] int32_t find(std::string_view name) const;
};

/// Read a .pcf: a binary DMX of format "pcf" whose root lists `particleSystemDefinitions`.
/// Every DmeParticleSystemDefinition in the file is a system, listed or not, in file order,
/// since a child may be one the root does not list.
[[nodiscard]] PcfFile read_pcf(std::span<const std::byte> data);

} // namespace quebratsk::parsers::source1
