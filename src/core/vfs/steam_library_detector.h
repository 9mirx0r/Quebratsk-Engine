#pragma once

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <string>
#include <vector>

namespace quebratsk::vfs {

class SteamLibraryDetector : public godot::Object {
    GDCLASS(SteamLibraryDetector, godot::Object)

protected:
    static void _bind_methods();

public:
    SteamLibraryDetector() = default;
    ~SteamLibraryDetector() = default;

    /// Automatically scans Windows Registry / Steam libraryfolders.vdf
    /// to detect installed legacy games (Half-Life, CS 1.6, Arma 3, CS2, DayZ).
    /// Returns a Dictionary mapping GameName -> AbsoluteInstallPath.
    static godot::Dictionary detect_installed_games();

private:
    static std::string _get_steam_install_path();
    static std::vector<std::string> _get_library_folders(const std::string& steam_path);
};

} // namespace quebratsk::vfs
