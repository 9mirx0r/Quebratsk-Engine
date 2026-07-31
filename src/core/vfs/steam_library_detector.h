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

    /// Find supported games installed on this machine.
    ///
    /// Steam is searched first, through the registry and libraryfolders.vdf so copies on
    /// secondary drives are found, then GOG, Epic and the usual manual install locations.
    /// Detecting only Steam told everyone else "no installed games found" while the game
    /// sat on their drive.
    ///
    /// Returns a Dictionary mapping display name to absolute install path. A game
    /// installed twice keeps the Steam copy, because that list is scanned first.
    static godot::Dictionary detect_installed_games();

private:
    static std::string _get_steam_install_path();
    static std::vector<std::string> _get_library_folders(const std::string& steam_path);

    /// Library roots belonging to launchers other than Steam, plus conventional
    /// hand-install directories.
    static std::vector<std::string> _get_other_launcher_folders();

    /// Read a REG_SZ value, or "" when absent. Windows only; returns "" elsewhere.
    static std::string _read_registry_string(void* root, const char* subkey,
                                             const char* value);
};

} // namespace quebratsk::vfs
