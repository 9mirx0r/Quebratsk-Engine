#include "steam_library_detector.h"
#include <filesystem>
#include <fstream>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace quebratsk::vfs {

using namespace godot;

void SteamLibraryDetector::_bind_methods() {
    ClassDB::bind_static_method("SteamLibraryDetector", D_METHOD("detect_installed_games"), &SteamLibraryDetector::detect_installed_games);
}

std::string SteamLibraryDetector::_get_steam_install_path() {
#if defined(_WIN32)
    // RegQueryValueExA does NOT guarantee a NUL terminator for REG_SZ values: if the
    // stored string exactly fills the buffer, std::string(path) reads off the end of the
    // stack array. RegGetValueA guarantees termination and filters by value type.
    char path[MAX_PATH + 1] = {};
    DWORD buffer_size = MAX_PATH;

    if (RegGetValueA(HKEY_CURRENT_USER, "Software\\Valve\\Steam", "SteamPath",
                     RRF_RT_REG_SZ, nullptr, path, &buffer_size) == ERROR_SUCCESS) {
        path[MAX_PATH] = '\0';
        return std::string(path);
    }
#endif
    return "C:/Program Files (x86)/Steam"; // Default fallback
}

/// Read a REG_SZ value, or return "" when it is absent.
///
/// RegGetValueA guarantees NUL termination and filters by type; RegQueryValueExA does
/// neither, and a value that exactly fills the buffer reads off the end of it.
std::string SteamLibraryDetector::_read_registry_string(void* root, const char* subkey,
                                                        const char* value) {
#if defined(_WIN32)
    char buffer[MAX_PATH + 1] = {};
    DWORD size = MAX_PATH;
    if (RegGetValueA(static_cast<HKEY>(root), subkey, value, RRF_RT_REG_SZ, nullptr,
                     buffer, &size) == ERROR_SUCCESS) {
        buffer[MAX_PATH] = '\0';
        return std::string(buffer);
    }
#else
    (void)root; (void)subkey; (void)value;
#endif
    return {};
}

/// Library roots that are not Steam.
///
/// Steam is where most of these games are, but not where all of them are: GOG sells the
/// Half-Life and Arma catalogues, Epic has given several away, and plenty of people still
/// have a copy installed by hand from a disc. Detecting only Steam meant telling those
/// users "no installed games found" while the game sat on their drive.
std::vector<std::string> SteamLibraryDetector::_get_other_launcher_folders() {
    std::vector<std::string> roots;

#if defined(_WIN32)
    // GOG Galaxy and the Epic launcher both record their install root in the registry.
    for (const auto& [key, name] : {
             std::pair{"SOFTWARE\\WOW6432Node\\GOG.com\\GalaxyClient\\paths", "client"},
             std::pair{"SOFTWARE\\GOG.com\\GalaxyClient\\paths", "client"},
         }) {
        std::string path = _read_registry_string(HKEY_LOCAL_MACHINE, key, name);
        if (!path.empty()) {
            roots.push_back(std::filesystem::path(path).parent_path().string() + "/Games");
        }
    }

    std::string epic = _read_registry_string(
        HKEY_LOCAL_MACHINE, "SOFTWARE\\WOW6432Node\\Epic Games\\EpicGamesLauncher",
        "AppDataPath");
    if (!epic.empty()) {
        roots.push_back("C:/Program Files/Epic Games");
    }

    // Conventional manual-install locations. Cheap to probe and they cover the copies
    // that no launcher knows about.
    roots.push_back("C:/GOG Games");
    roots.push_back("C:/Games");
    roots.push_back("C:/Program Files (x86)/Steam/steamapps/common");
#endif

    return roots;
}

std::vector<std::string> SteamLibraryDetector::_get_library_folders(const std::string& steam_path) {
    std::vector<std::string> folders;
    folders.push_back(steam_path + "/steamapps/common");

    std::string vdf_path = steam_path + "/steamapps/libraryfolders.vdf";
    std::ifstream file(vdf_path);
    if (!file.is_open()) return folders;

    std::string line;
    while (std::getline(file, line)) {
        size_t pos = line.find("\"path\"");
        if (pos != std::string::npos) {
            size_t first_quote = line.find('"', pos + 6);
            size_t second_quote = line.find('"', first_quote + 1);
            if (first_quote != std::string::npos && second_quote != std::string::npos) {
                std::string folder = line.substr(first_quote + 1, second_quote - first_quote - 1);
                // Fix escaped backslashes
                std::string clean_folder;
                for (char c : folder) {
                    if (c != '\\') clean_folder += c;
                    else clean_folder += '/';
                }
                folders.push_back(clean_folder + "/steamapps/common");
            }
        }
    }

    return folders;
}

Dictionary SteamLibraryDetector::detect_installed_games() {
    Dictionary installed_games;
    std::string steam_path = _get_steam_install_path();
    std::vector<std::string> libraries = _get_library_folders(steam_path);

    struct TargetGame {
        std::string name;
        std::string folder_name;
    };

    std::vector<TargetGame> targets = {
        {"Half-Life", "Half-Life"},
        {"Counter-Strike 1.6", "Half-Life/cstrike"},
        {"Half-Life 2", "Half-Life 2"},
        {"Half-Life 2: Episode One", "Half-Life 2/episodic"},
        {"Half-Life 2: Episode Two", "Half-Life 2/ep2"},
        {"Garry's Mod", "GarrysMod"},
        {"Portal", "Portal"},
        {"Portal 2", "Portal 2"},
        {"Team Fortress 2", "Team Fortress 2"},
        {"Left 4 Dead 2", "Left 4 Dead 2"},
        {"Counter-Strike 2", "Counter-Strike Global Offensive"},
        {"Arma 3", "Arma 3"},
        {"DayZ", "DayZ"}
    };

    // Steam libraries first, then everything else, so a Steam copy wins the name when a
    // game is installed twice. Duplicate keys would otherwise resolve by scan order.
    for (const auto& root : _get_other_launcher_folders()) {
        libraries.push_back(root);
    }

    for (const auto& lib : libraries) {
        for (const auto& target : targets) {
            std::filesystem::path full_path = std::filesystem::path(lib) / target.folder_name;
            // error_code overload: the throwing one aborts under _HAS_EXCEPTIONS=0 on
            // unreadable drives or malformed paths from libraryfolders.vdf.
            std::error_code ec;
            if (std::filesystem::exists(full_path, ec) && !ec) {
                // First hit wins: libraries are ordered Steam-first above.
                const String key(target.name.c_str());
                if (!installed_games.has(key)) {
                    installed_games[key] = String(full_path.string().c_str());
                }
            }
        }
    }

    return installed_games;
}

} // namespace quebratsk::vfs
