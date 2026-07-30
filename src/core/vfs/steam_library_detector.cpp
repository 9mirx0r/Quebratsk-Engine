#include "steam_library_detector.h"
#include <filesystem>
#include <fstream>
#include <iostream>

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
    HKEY hKey;
    char path[MAX_PATH];
    DWORD bufferSize = sizeof(path);

    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Valve\\Steam", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        if (RegQueryValueExA(hKey, "SteamPath", NULL, NULL, (LPBYTE)path, &bufferSize) == ERROR_SUCCESS) {
            RegCloseKey(hKey);
            return std::string(path);
        }
        RegCloseKey(hKey);
    }
#endif
    return "C:/Program Files (x86)/Steam"; // Default fallback
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
        {"Garry's Mod", "GarrysMod"},
        {"Counter-Strike 2", "Counter-Strike Global Offensive"},
        {"Arma 3", "Arma 3"},
        {"DayZ", "DayZ"}
    };

    for (const auto& lib : libraries) {
        for (const auto& target : targets) {
            std::filesystem::path full_path = std::filesystem::path(lib) / target.folder_name;
            if (std::filesystem::exists(full_path)) {
                installed_games[String(target.name.c_str())] = String(full_path.string().c_str());
            }
        }
    }

    return installed_games;
}

} // namespace quebratsk::vfs
