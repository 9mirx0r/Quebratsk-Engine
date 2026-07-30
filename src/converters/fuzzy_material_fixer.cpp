#include "fuzzy_material_fixer.h"
#include <algorithm>
#include <cctype>
#include <string_view>
#include <vector>

namespace quebratsk::converters {

using namespace godot;

namespace {

/// Strip the directory prefix and the extension: "vfs://cs16/materials/props/x.vtf" -> "x".
/// Comparing full URIs made the Levenshtein distance dominated by the shared path
/// prefix, so the "closest" match was effectively arbitrary.
std::string basename_no_ext(std::string_view uri) {
    const size_t slash = uri.find_last_of("/\\");
    if (slash != std::string_view::npos) {
        uri = uri.substr(slash + 1);
    }
    const size_t dot = uri.find_last_of('.');
    if (dot != std::string_view::npos) {
        uri = uri.substr(0, dot);
    }
    std::string out(uri);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

bool has_image_extension(std::string_view path) {
    static constexpr std::string_view kExts[] = {".vtf", ".paa", ".png", ".tga", ".jpg", ".dds", ".bmp"};
    const size_t dot = path.find_last_of('.');
    if (dot == std::string_view::npos) return false;

    std::string ext(path.substr(dot));
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return std::find(std::begin(kExts), std::end(kExts), ext) != std::end(kExts);
}

} // namespace

void FuzzyMaterialFixer::_bind_methods() {
    ClassDB::bind_static_method("FuzzyMaterialFixer", D_METHOD("find_best_matching_texture", "missing_texture_name", "vfs"), &FuzzyMaterialFixer::find_best_matching_texture);
}

size_t FuzzyMaterialFixer::_levenshtein_distance(const std::string& s1, const std::string& s2) {
    const size_t m = s1.size();
    const size_t n = s2.size();
    if (m == 0) return n;
    if (n == 0) return m;

    std::vector<size_t> costs(n + 1);
    for (size_t k = 0; k <= n; ++k) costs[k] = k;

    size_t i = 0;
    for (char c1 : s1) {
        costs[0] = i + 1;
        size_t corner = i;

        size_t j = 0;
        for (char c2 : s2) {
            size_t upper = costs[j + 1];
            // Inputs are pre-lowercased by the caller. Passing a raw (possibly
            // negative) char to std::tolower is undefined behaviour.
            if (c1 == c2) {
                costs[j + 1] = corner;
            } else {
                size_t t = std::min(upper, corner);
                costs[j + 1] = std::min(costs[j], t) + 1;
            }
            corner = upper;
            j++;
        }
        i++;
    }
    return costs[n];
}

String FuzzyMaterialFixer::find_best_matching_texture(const String& missing_texture_name, quebratsk::vfs::VFSManager* vfs) {
    if (!vfs || missing_texture_name.is_empty()) return "";

    const std::string target = basename_no_ext(missing_texture_name.utf8().get_data());
    if (target.empty()) return "";

    PackedStringArray all_files = vfs->list_files("");

    std::string best_match;
    size_t min_distance = std::string::npos;

    for (int i = 0; i < all_files.size(); ++i) {
        const std::string candidate = all_files[i].utf8().get_data();
        if (!has_image_extension(candidate)) continue;

        const std::string cand_base = basename_no_ext(candidate);
        if (cand_base.empty()) continue;

        // A length difference is a lower bound on the edit distance, so this prunes
        // most candidates before paying for the O(n*m) table.
        const size_t len_gap = target.size() > cand_base.size()
                             ? target.size() - cand_base.size()
                             : cand_base.size() - target.size();
        if (min_distance != std::string::npos && len_gap >= min_distance) continue;

        const size_t dist = _levenshtein_distance(target, cand_base);
        if (dist < min_distance) {
            min_distance = dist;
            best_match = candidate;
            if (dist == 0) break; // exact basename match, cannot do better
        }
    }

    // Without a ceiling this always returned *something*, however unrelated. Require
    // the match to share at least two thirds of the name.
    const size_t max_acceptable = target.size() / 3;
    if (min_distance == std::string::npos || min_distance > max_acceptable) {
        return "";
    }

    return String(best_match.c_str());
}

} // namespace quebratsk::converters
