// Verification of the Source material reader (src/parsers/source1/vmt_parser.cpp).
//
// A .vmt is KeyValues text: a shader name, then "$parameter" "value" pairs in braces,
// with sub-blocks (Proxies, fallbacks) whose keys are not the material's own. Each case
// below is a material written the way real ones are written, with what the reader must
// take from it. Exits non-zero on failure.
#include "parsers/source1/vmt_parser.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

using namespace quebratsk::parsers::source1;
using quebratsk::ir::IRMaterialData;

static int failures = 0;

static void check(const char* what, bool ok) {
    if (!ok) {
        std::printf("  FAIL %s\n", what);
        ++failures;
    }
}

static void check_str(const char* what, const std::string& got, const std::string& want) {
    if (got != want) {
        std::printf("  FAIL %-40s got \"%s\" want \"%s\"\n", what, got.c_str(), want.c_str());
        ++failures;
    }
}

static IRMaterialData parse(std::string_view text, const char* name) {
    const auto bytes = std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(text.data()), text.size());
    auto res = VMTParser::parse(bytes);
    if (!res) {
        std::printf("  FAIL %s refused\n", name);
        ++failures;
        return {};
    }
    return *res;
}

static std::string albedo(const IRMaterialData& m) {
    return m.albedo_texture ? m.albedo_texture->vfs_path : std::string("<none>");
}

/// The base texture as a plain line-by-line look finds it: a line, at the material's own
/// level, whose first word is $basetexture. Only used to cross-check real files, where
/// one pair per line is how nearly all of them are written.
static std::string line_by_line_basetexture(const std::string& text) {
    std::istringstream in(text);
    std::string line;
    int depth = 0;
    std::string found = "<none>";
    while (std::getline(in, line)) {
        if (const size_t c = line.find("//"); c != std::string::npos) line.erase(c);
        line.erase(std::remove(line.begin(), line.end(), '"'), line.end());
        std::istringstream words(line);
        std::string key, value;
        words >> key >> value;
        std::string lower = key;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        if (depth == 1 && lower == "$basetexture" && !value.empty() && value[0] != '{' &&
            value[0] != '[') {
            found = value;
        }
        depth += static_cast<int>(std::count(line.begin(), line.end(), '{'));
        depth -= static_cast<int>(std::count(line.begin(), line.end(), '}'));
        if (depth < 0) depth = 0;
    }
    return found;
}

static std::string lower(std::string s) {
    std::replace(s.begin(), s.end(), '\\', '/');
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return s;
}

static std::vector<std::byte> to_bytes(const std::string& s) {
    const auto* p = reinterpret_cast<const std::byte*>(s.data());
    return std::vector<std::byte>(p, p + s.size());
}

/// The include path a patch names, the way a line-by-line look finds it.
static std::string line_by_line_include(const std::string& text) {
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        line.erase(std::remove(line.begin(), line.end(), '"'), line.end());
        std::istringstream words(line);
        std::string key, value;
        words >> key >> value;
        if (lower(key) == "include") return lower(value);
    }
    return {};
}

/// Every .vmt under `root`: the reader must take each one, and find the same base texture
/// a line-by-line look finds. A patch is followed to the material it includes, looked up
/// under `root`, and its texture compared with that material's. Run by hand with a folder
/// of game materials; nothing from a game is kept in the repository.
static int corpus(const char* root) {
    namespace fs = std::filesystem;
    std::map<std::string, std::string> by_path; // "materials/x/y.vmt" -> text
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(root, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        if (!it->is_regular_file()) continue;
        if (lower(it->path().extension().string()) != ".vmt") continue;
        std::ifstream f(it->path(), std::ios::binary);
        by_path[lower(fs::relative(it->path(), root).generic_string())] =
            std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    }

    const VMTParser::IncludeReader read_include = [&](std::string_view path) {
        const auto it = by_path.find(lower(std::string(path)));
        return it == by_path.end() ? std::vector<std::byte>{} : to_bytes(it->second);
    };

    int files = 0, refused = 0, differ = 0, shown = 0, patches = 0, followed = 0;
    for (const auto& [path, text] : by_path) {
        ++files;
        const auto bytes = to_bytes(text);
        const auto res = VMTParser::parse(bytes, read_include);
        if (!res) {
            ++refused;
            continue;
        }
        std::string want = line_by_line_basetexture(text);
        if (const std::string inc = line_by_line_include(text); !inc.empty()) {
            ++patches;
            if (const auto base = by_path.find(inc); base != by_path.end()) {
                ++followed;
                if (want == "<none>") want = line_by_line_basetexture(base->second);
            }
        }
        const std::string got = albedo(*res);
        if (got != want) {
            ++differ;
            if (shown++ < 15) {
                std::printf("  differs %s\n    reader \"%s\"  line look \"%s\"\n",
                            path.c_str(), got.c_str(), want.c_str());
            }
        }
    }
    std::printf("\n  %d material(s), %d refused, %d differ; %d patch(es), %d with their base here\n",
                files, refused, differ, patches, followed);
    return 0;
}

int main(int argc, char** argv) {
    std::printf("VMT parser verification\n\n");
    if (argc > 1) return corpus(argv[1]);

    // ---- the common case, quoted, one pair per line ----------------------------
    {
        const auto m = parse(
            "\"VertexLitGeneric\"\n"
            "{\n"
            "\t\"$basetexture\" \"models/weapons/ak/body\"\n"
            "\t\"$bumpmap\" \"models/weapons/ak/body_normal\"\n"
            "\t\"$translucent\" \"1\"\n"
            "\t\"$nocull\" \"1\"\n"
            "}\n", "quoted");
        check_str("quoted shader", m.shader_name, "VertexLitGeneric");
        check_str("quoted basetexture", albedo(m), "models/weapons/ak/body");
        check_str("quoted bumpmap", m.normal_texture ? m.normal_texture->vfs_path : "<none>",
                  "models/weapons/ak/body_normal");
        check("quoted translucent", m.is_transparent);
        check("quoted nocull", m.is_two_sided);
    }

    // ---- unquoted keys, mixed case, tabs ---------------------------------------
    {
        const auto m = parse(
            "LightmappedGeneric\n"
            "{\n"
            "\t$BaseTexture\tconcrete/wall01\n"
            "\t$AlphaTest 1\n"
            "}\n", "unquoted");
        check_str("unquoted shader", m.shader_name, "LightmappedGeneric");
        check_str("unquoted basetexture", albedo(m), "concrete/wall01");
        check("unquoted alphatest -> transparent", m.is_transparent);
    }

    // ---- keys that start like another key ---------------------------------------
    {
        // $basetexturetransform and $basetexture2 (the second layer of a blend) are not
        // the base texture; $detailscale is not a detail texture.
        const auto m = parse(
            "WorldVertexTransition\n"
            "{\n"
            "\t$basetexturetransform \"center .5 .5 scale 2 2 rotate 0 translate 0 0\"\n"
            "\t$basetexture2 nature/grass01\n"
            "\t$detailscale 4\n"
            "\t$basetexture nature/dirt01\n"
            "}\n", "prefixes");
        check_str("prefix basetexture", albedo(m), "nature/dirt01");
        check("prefix detailscale is not a detail texture", !m.detail_texture.has_value());
    }

    // ---- sub-blocks are not the material's own keys -----------------------------
    {
        const auto m = parse(
            "UnlitGeneric\n"
            "{\n"
            "\t$basetexture effects/screen\n"
            "\tProxies\n"
            "\t{\n"
            "\t\tAnimatedTexture\n"
            "\t\t{\n"
            "\t\t\tanimatedtexturevar $basetexture\n"
            "\t\t\t$additive 1\n"
            "\t\t}\n"
            "\t}\n"
            "}\n", "proxies");
        check_str("proxies basetexture untouched", albedo(m), "effects/screen");
        check("proxies $additive inside a proxy ignored", !m.is_additive);
    }

    // ---- comments ---------------------------------------------------------------
    {
        const auto m = parse(
            "VertexLitGeneric\n"
            "{\n"
            "\t// $basetexture wrong/one\n"
            "\t$basetexture right/one // the one in use\n"
            "\t$additive 0\n"
            "}\n", "comments");
        check_str("comment basetexture", albedo(m), "right/one");
        check("comment additive 0", !m.is_additive);
    }

    // ---- everything on one line --------------------------------------------------
    {
        // Materials packed into maps by tools are often written this way.
        const auto m = parse("\"UnlitGeneric\" { \"$basetexture\" \"sprites/glow\" \"$additive\" \"1\" }",
                             "one line");
        check_str("one-line shader", m.shader_name, "UnlitGeneric");
        check_str("one-line basetexture", albedo(m), "sprites/glow");
        check("one-line additive", m.is_additive);
    }

    // ---- flags written as 0 -------------------------------------------------------
    {
        const auto m = parse("VertexLitGeneric\n{\n$translucent 0\n$alphatest 0\n$nocull 0\n}\n",
                             "zeros");
        check("zeros not transparent", !m.is_transparent);
        check("zeros not two-sided", !m.is_two_sided);
    }

    // ---- patch: the base material, with the patch's changes on top ----------------
    {
        // The way a map compiler writes them: the face's real material, $envmap changed.
        const std::string base =
            "LightmappedGeneric\n"
            "{\n"
            "\t$basetexture de_shipped/deck01\n"
            "\t$bumpmap de_shipped/deck01_normal\n"
            "\t$envmap env_cubemap\n"
            "\t$nocull 1\n"
            "}\n";
        const std::string other = "VertexLitGeneric { $basetexture shared/other }";
        const std::string chained = "patch { include materials/base.vmt }";
        const std::string self = "patch { include materials/self.vmt }";
        const VMTParser::IncludeReader read = [&](std::string_view path) {
            const std::string p = lower(std::string(path));
            if (p == "materials/base.vmt") return to_bytes(base);
            if (p == "materials/other.vmt") return to_bytes(other);
            if (p == "materials/chained.vmt") return to_bytes(chained);
            if (p == "materials/self.vmt") return to_bytes(self);
            return std::vector<std::byte>{};
        };
        auto parse_patch = [&](const std::string& text, const char* name) {
            const auto bytes = to_bytes(text);
            auto res = VMTParser::parse(bytes, read);
            if (!res) {
                std::printf("  FAIL %s refused\n", name);
                ++failures;
                return IRMaterialData{};
            }
            return *res;
        };

        {
            const auto m = parse_patch(
                "\"patch\"\n{\n\t\"include\" \"materials\\BASE.vmt\"\n"
                "\t\"replace\"\n\t{\n\t\t\"$envmap\" \"maps/x/c0_0_0\"\n\t}\n}\n",
                "patch replace");
            check_str("patch shader is the base's", m.shader_name, "LightmappedGeneric");
            check_str("patch basetexture from base", albedo(m), "de_shipped/deck01");
            check("patch keeps base nocull", m.is_two_sided);
        }
        {
            const auto m = parse_patch(
                "patch\n{\n"
                "\tinclude materials/base.vmt\n"
                "\tinsert\n\t{\n"
                "\t\t$basetexture custom/deck\n"
                "\t\t$bumpmap \"\"\n"
                "\t\t$additive 1\n"
                "\t}\n"
                "\treplace\n\t{\n"
                "\t\t$translucent 1\n"
                "\t}\n"
                "}\n",
                "patch insert");
            check_str("insert overrides basetexture", albedo(m), "custom/deck");
            check("insert \"\" removes bumpmap", !m.normal_texture.has_value());
            check("insert adds additive", m.is_additive);
            check("replace adds nothing the base lacks", !m.is_transparent);
        }
        {
            const auto m = parse_patch("patch { include materials/chained.vmt insert { $additive 1 } }",
                                       "patch chain");
            check_str("chain reaches the base", albedo(m), "de_shipped/deck01");
            check("chain keeps its own insert", m.is_additive);
        }
        {
            const auto m = parse_patch("patch { include materials/self.vmt }", "patch loop");
            check_str("loop ends with no texture", albedo(m), "<none>");
        }
        {
            const auto m = parse_patch(
                "patch { include materials/missing.vmt replace { $envmap x } }", "patch missing");
            check_str("missing base: shader stays patch", m.shader_name, "patch");
            check_str("missing base: no texture", albedo(m), "<none>");
        }
        {
            // Without a way to read the base, only the patch's own inserts count.
            const auto m = parse("patch { include materials/base.vmt insert { $basetexture a/b } }",
                                 "patch alone");
            check_str("patch alone basetexture", albedo(m), "a/b");
        }
        {
            // A pair inside insert is not the patch's own include or texture.
            const auto m = parse_patch(
                "patch { include materials/other.vmt insert { include materials/base.vmt } }",
                "patch nested include");
            check_str("include inside insert is not followed", albedo(m), "shared/other");
        }
    }

    // ---- empty file ----------------------------------------------------------------
    {
        const std::span<const std::byte> none;
        check("empty refused", !VMTParser::parse(none).has_value());
    }

    if (failures == 0) std::printf("  all checks passed\n");
    return failures == 0 ? 0 : 1;
}
