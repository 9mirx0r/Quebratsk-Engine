// Verification of the binary DMX reader and the particle file reader
// (src/parsers/source1/dmx_reader.cpp, pcf_reader.cpp).
//
// The documents below are written by hand in each of the five binary encodings, laid out as
// the Valve Developer Wiki's DMX/Binary page describes them, so every value read back is
// known. Given a .pcf or a folder, it also reads every particle file there and checks each is
// read to its last byte. Exits non-zero on failure.
#include "parsers/source1/pcf_reader.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace quebratsk::parsers::source1;

static int failures = 0;

static void check(const char* what, long long got, long long want) {
    if (got != want) {
        std::printf("  FAIL %-44s got %lld want %lld\n", what, got, want);
        ++failures;
    }
}

static void check_near(const char* what, double got, double want) {
    if (std::fabs(got - want) > 1e-5) {
        std::printf("  FAIL %-44s got %f want %f\n", what, got, want);
        ++failures;
    }
}

static void check_str(const char* what, const std::string& got, const std::string& want) {
    if (got != want) {
        std::printf("  FAIL %-44s got \"%s\" want \"%s\"\n", what, got.c_str(), want.c_str());
        ++failures;
    }
}

// ------------------------------------------------------------- a DMX writer ----

struct WAttr {
    std::string name;
    uint8_t type = 0;           // DmxType, plus 14 for an array
    std::vector<double> numbers;
    std::vector<std::string> texts;
    std::vector<int32_t> elements;
    std::vector<std::string> outside;   // ids of elements kept in another file
};

struct WElem {
    std::string type;
    std::string name;
    std::vector<WAttr> attrs;
};

class Writer {
public:
    explicit Writer(int version) : _v(version) {}

    std::vector<std::byte> write(const std::vector<WElem>& elems, const std::string& format = "pcf") {
        std::vector<std::string> table;
        auto add = [&](const std::string& s) {
            for (const auto& t : table) if (t == s) return;
            table.push_back(s);
        };
        if (_v >= 2) {
            for (const auto& e : elems) {
                add(e.type);
                if (_v >= 4) add(e.name);
                for (const auto& a : e.attrs) {
                    add(a.name);
                    if (_v >= 4 && a.type == 5) add(a.texts[0]);
                }
            }
        }
        _table = table;
        raw("<!-- dmx encoding binary " + std::to_string(_v) + " format " + format + " 1 -->\n");
        byte(0);
        if (_v >= 2) {
            if (_v >= 4) put<int32_t>(static_cast<int32_t>(table.size()));
            else put<int16_t>(static_cast<int16_t>(table.size()));
            for (const auto& s : table) { raw(s); byte(0); }
        }
        put<int32_t>(static_cast<int32_t>(elems.size()));
        for (size_t i = 0; i < elems.size(); ++i) {
            name(elems[i].type);
            value_string(elems[i].name);
            for (int b = 0; b < 16; ++b) byte(static_cast<uint8_t>(i * 16 + b));
        }
        for (const auto& e : elems) {
            put<int32_t>(static_cast<int32_t>(e.attrs.size()));
            for (const auto& a : e.attrs) {
                name(a.name);
                byte(a.type);
                const bool array = a.type > 14;
                const uint8_t base = array ? a.type - 14 : a.type;
                if (array) {
                    size_t n = 0;
                    if (base == 1) n = a.elements.size() + a.outside.size();
                    else if (base == 5) n = a.texts.size();
                    else n = a.numbers.size() / std::max<size_t>(dmx_components(DmxType(base)), 1);
                    put<int32_t>(static_cast<int32_t>(n));
                }
                if (base == 1) {
                    for (const int32_t e2 : a.elements) put<int32_t>(e2);
                    for (const auto& id : a.outside) { put<int32_t>(-2); raw(id); byte(0); }
                } else if (base == 5) {
                    for (const auto& t : a.texts) {
                        if (array) { raw(t); byte(0); } else value_string(t);
                    }
                } else if (base == 4) {
                    for (const double d : a.numbers) byte(d != 0 ? 1 : 0);
                } else if (base == 2) {
                    for (const double d : a.numbers) put<int32_t>(static_cast<int32_t>(d));
                } else if (base == 7) {
                    for (const double d : a.numbers) put<int32_t>(static_cast<int32_t>(std::lround(d * 10000)));
                } else if (base == 8) {
                    for (const double d : a.numbers) byte(static_cast<uint8_t>(d));
                } else {
                    for (const double d : a.numbers) put<float>(static_cast<float>(d));
                }
            }
        }
        return _out;
    }

private:
    template <typename T>
    void put(T v) {
        const auto* p = reinterpret_cast<const std::byte*>(&v);
        _out.insert(_out.end(), p, p + sizeof(T));
    }
    void byte(uint8_t b) { _out.push_back(std::byte{b}); }
    void raw(const std::string& s) {
        const auto* p = reinterpret_cast<const std::byte*>(s.data());
        _out.insert(_out.end(), p, p + s.size());
    }
    void index(const std::string& s) {
        int32_t i = 0;
        for (; i < static_cast<int32_t>(_table.size()); ++i) if (_table[static_cast<size_t>(i)] == s) break;
        if (_v >= 5) put<int32_t>(i); else put<int16_t>(static_cast<int16_t>(i));
    }
    void name(const std::string& s) {
        if (_v >= 2) index(s); else { raw(s); byte(0); }
    }
    void value_string(const std::string& s) {
        if (_v >= 4) index(s); else { raw(s); byte(0); }
    }

    int _v;
    std::vector<std::string> _table;
    std::vector<std::byte> _out;
};

static WAttr str(const std::string& n, const std::string& v) { WAttr a; a.name = n; a.type = 5; a.texts = {v}; return a; }
static WAttr num(const std::string& n, uint8_t t, std::vector<double> v) { WAttr a; a.name = n; a.type = t; a.numbers = std::move(v); return a; }
static WAttr link(const std::string& n, std::vector<int32_t> v, bool array = true) {
    WAttr a; a.name = n; a.type = array ? 15 : 1; a.elements = std::move(v); return a;
}

/// A flash with one child, as the particle editor would save it.
static std::vector<WElem> flash(int version) {
    std::vector<WElem> e(6);
    e[0] = {"DmElement", "untitled", {link("particleSystemDefinitions", {1, 4})}};
    e[1] = {"DmeParticleSystemDefinition", "Flash_A", {
        str("material", "effects\\muzzle.vmt"),
        num("max_particles", 2, {8}),
        num("color", 8, {255, 200, 100, 255}),
        num("bounding_box_min", 10, {-1, -2, -3}),
        num("rotation", 3, {12.5}),
        link("emitters", {2}),
        link("operators", {}),
        link("renderers", {3}),
        link("children", {5}),
    }};
    if (version >= 3) e[1].attrs.push_back(num("duration", 7, {0.25}));
    WAttr names; names.name = "sequences"; names.type = 5 + 14; names.texts = {"one", "two"};
    WAttr far_off = link("elsewhere", {}); far_off.outside = {"8f1f2a3b-0000-0000-0000-000000000001"};
    e[2] = {"DmeParticleOperator", "emit_instantaneously", {
        str("functionName", "emit_instantaneously"),
        num("num_to_emit", 2, {3}),
        num("emission_start_time", 3, {0.0}),
    }};
    e[3] = {"DmeParticleOperator", "render_animated_sprites", {
        str("functionName", "render_animated_sprites"),
        num("animation rate", 3, {1.5}),
        num("orientation_type", 2, {0}),
        num("additive", 4, {1}),
        names,
        far_off,
        num("corners", 10 + 14, {1, 2, 3, 4, 5, 6}),
    }};
    e[4] = {"DmeParticleSystemDefinition", "flash_b", {link("renderers", {}), link("children", {})}};
    e[5] = {"DmeParticleChild", "flash_b", {link("child", {4}, false), num("delay", 3, {0.1})}};
    return e;
}

static void test_version(int version) {
    std::printf("encoding %d\n", version);
    const auto bytes = Writer(version).write(flash(version));
    const PcfFile f = read_pcf(bytes);
    if (!f.dmx.error.empty()) std::printf("  error: %s\n", f.dmx.error.c_str());
    check("valid", f.valid(), 1);
    check("read to the end", static_cast<long long>(f.dmx.end), static_cast<long long>(bytes.size()));
    check("encoding", f.dmx.encoding_version, version);
    check_str("format", f.dmx.format, "pcf");
    check("elements", static_cast<long long>(f.dmx.elements.size()), 6);
    check("systems", static_cast<long long>(f.systems.size()), 2);
    if (f.systems.size() != 2) return;
    const PcfSystem& a = f.systems[0];
    check_str("first system", a.name, "Flash_A");
    check("found in any case", f.find("FLASH_a"), 0);
    check("not found", f.find("flash_c"), -1);
    check("emitters", static_cast<long long>(a.emitters.size()), 1);
    check("operators", static_cast<long long>(a.operators.size()), 0);
    check("renderers", static_cast<long long>(a.renderers.size()), 1);
    if (a.emitters.size() == 1) check_str("emitter", a.emitters[0].function, "emit_instantaneously");
    check("children", static_cast<long long>(a.children.size()), 1);
    if (a.children.size() == 1) {
        check("child is the second system", a.children[0].system, 1);
        check_near("child delay", a.children[0].delay, 0.1);
    }

    const DmxElement& sys = f.dmx.elements[1];
    const DmxAttribute* mat = sys.find("material");
    check_str("material", mat ? mat->text() : "", "effects\\muzzle.vmt");
    const DmxAttribute* most = sys.find("max_particles");
    check("max_particles", most ? static_cast<long long>(most->number()) : -1, 8);
    const DmxAttribute* col = sys.find("color");
    check("colour green", col ? static_cast<long long>(col->number(1)) : -1, 200);
    const DmxAttribute* box = sys.find("bounding_box_min");
    check_near("box z", box ? box->number(2) : 0, -3);
    const DmxAttribute* rot = sys.find("rotation");
    check_near("rotation", rot ? rot->number() : 0, 12.5);
    if (version >= 3) {
        const DmxAttribute* t = sys.find("duration");
        check_near("time", t ? t->number() : 0, 0.25);
    }

    const DmxElement& emit = f.dmx.elements[2];
    const DmxAttribute* n = emit.find("num_to_emit");
    check("num_to_emit", n ? static_cast<long long>(n->number()) : -1, 3);

    const DmxElement& render = f.dmx.elements[3];
    const DmxAttribute* add = render.find("additive");
    check("bool", add ? static_cast<long long>(add->number()) : -1, 1);
    const DmxAttribute* seq = render.find("sequences");
    check("string array", seq ? static_cast<long long>(seq->texts.size()) : -1, 2);
    if (seq && seq->texts.size() == 2) check_str("string array second", seq->texts[1], "two");
    const DmxAttribute* far_off = render.find("elsewhere");
    check("element kept elsewhere", far_off ? far_off->element() : 0, -1);
    const DmxAttribute* corners = render.find("corners");
    check("vector3 array values", corners ? static_cast<long long>(corners->count) : -1, 2);
    check_near("vector3 array last", corners ? corners->number(5) : 0, 6);
}

static void test_broken() {
    std::printf("broken files\n");
    const auto bytes = Writer(5).write(flash(5));
    // Cut anywhere, it is never half read.
    long long half_read = 0;
    for (size_t cut = 0; cut < bytes.size(); ++cut) {
        const auto f = read_pcf(std::span<const std::byte>(bytes.data(), cut));
        if (f.valid()) ++half_read;
    }
    check("cut files read as whole", half_read, 0);

    // A type number past the last kind, in a file otherwise whole.
    auto broken = flash(2);
    WAttr odd; odd.name = "odd"; odd.type = 40;
    broken[3].attrs.push_back(odd);
    const auto bad = read_pcf(Writer(2).write(broken));
    check("unknown type refused", bad.valid(), 0);
    check("names the attribute", bad.dmx.error.find("odd") != std::string::npos, 1);

    const std::string text = "<!-- dmx encoding keyvalues2 1 format pcf 1 -->\n\"DmElement\" {}";
    std::vector<std::byte> t(text.size() + 1, std::byte{0});
    std::memcpy(t.data(), text.data(), text.size());
    const auto kv = read_pcf(t);
    check("text encoding refused", kv.valid(), 0);
    check("with a reason", kv.dmx.error.empty(), 0);

    const auto model = read_pcf(Writer(5).write(flash(5), "model"));
    check("a model is not a particle file", model.valid(), 0);
    check("but is a document", model.dmx.elements.size() == 6, 1);
}

/// Every .pcf at `path` (a file, or a folder searched all the way down).
static void test_files(const std::filesystem::path& path) {
    std::vector<std::filesystem::path> files;
    if (std::filesystem::is_directory(path)) {
        for (const auto& e : std::filesystem::recursive_directory_iterator(path)) {
            auto ext = e.path().extension().string();
            for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (e.is_regular_file() && ext == ".pcf") files.push_back(e.path());
        }
    } else {
        files.push_back(path);
    }
    long long systems = 0;
    long long refused = 0;
    for (const auto& p : files) {
        std::ifstream in(p, std::ios::binary);
        std::vector<char> raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(raw.data()), raw.size());
        const PcfFile f = read_pcf(bytes);
        if (!f.valid() || f.dmx.end != bytes.size()) {
            std::printf("  FAIL %s: %s (read %zu of %zu)\n", p.filename().string().c_str(),
                        f.dmx.error.c_str(), f.dmx.end, bytes.size());
            ++refused;
            continue;
        }
        long long steps = 0;
        for (const auto& s : f.systems) {
            steps += static_cast<long long>(s.emitters.size() + s.initializers.size() +
                                            s.operators.size() + s.renderers.size());
        }
        std::printf("  %-32s encoding %d, %3zu systems, %4lld steps\n", p.filename().string().c_str(),
                    f.dmx.encoding_version, f.systems.size(), steps);
        systems += static_cast<long long>(f.systems.size());
    }
    std::printf("  %zu file(s), %lld systems, %lld refused\n", files.size(), systems, refused);
    failures += static_cast<int>(refused);
}

int main(int argc, char** argv) {
    for (int v = 1; v <= 5; ++v) test_version(v);
    test_broken();
    if (argc > 1) test_files(argv[1]);
    if (failures == 0) std::printf("pcf_reader_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
