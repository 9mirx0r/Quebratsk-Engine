#include "fuzzy_material_fixer.h"
#include <algorithm>
#include <vector>

namespace quebratsk::converters {

using namespace godot;

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
            if (tolower(c1) == tolower(c2)) {
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

    std::string target = missing_texture_name.utf8().get_data();
    PackedStringArray all_files = vfs->list_files("");

    std::string best_match;
    size_t min_distance = std::string::npos;

    for (int i = 0; i < all_files.size(); ++i) {
        std::string candidate = all_files[i].utf8().get_data();
        
        // Only check image extensions
        if (candidate.find(".vtf") != std::string::npos || 
            candidate.find(".paa") != std::string::npos || 
            candidate.find(".png") != std::string::npos ||
            candidate.find(".tga") != std::string::npos) {
            
            size_t dist = _levenshtein_distance(target, candidate);
            if (dist < min_distance) {
                min_distance = dist;
                best_match = candidate;
            }
        }
    }

    return String(best_match.c_str());
}

} // namespace quebratsk::converters
