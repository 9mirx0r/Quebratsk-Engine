extends RefCounted

## Where the games are, for the suites in this folder.
##
## The suites used to carry "C:/Program Files (x86)/Steam/..." as constants. On a machine
## whose Steam library lives anywhere else they found nothing, checked nothing, and still
## ended cleanly. This asks the engine's own Steam detector where the libraries are, so a
## suite runs against whatever is really installed and says so when its game is not.
##
##   const GamePaths := preload("res://tests/game_paths.gd")
##   var root := GamePaths.common("Half-Life/cstrike")
##
## QUEBRATSK_STEAMAPPS overrides detection: point it at a `steamapps` folder.

const DEFAULT_STEAMAPPS := "C:/Program Files (x86)/Steam/steamapps"
const MARKER := "/steamapps/"


## Every `steamapps` folder known on this machine, most trusted first.
static func steamapps_dirs() -> PackedStringArray:
	var found := PackedStringArray()
	var override := OS.get_environment("QUEBRATSK_STEAMAPPS").replace("\\", "/")
	if not override.is_empty():
		found.append(override.trim_suffix("/"))

	var games: Dictionary = SteamLibraryDetector.detect_installed_games()
	for title in games:
		var root := str(games[title]).replace("\\", "/")
		var at := root.to_lower().find(MARKER)
		if at < 0:
			continue
		var steamapps := root.substr(0, at + MARKER.length() - 1)
		if not found.has(steamapps):
			found.append(steamapps)

	if not found.has(DEFAULT_STEAMAPPS):
		found.append(DEFAULT_STEAMAPPS)
	return found


## A folder under `steamapps`, e.g. "workshop/content/4000". When no library has it, the
## path under the first library is returned anyway, so the caller's own "not installed"
## message names a real place to look.
static func steamapps(relative: String) -> String:
	var dirs := steamapps_dirs()
	for dir in dirs:
		var candidate := str(dir).path_join(relative)
		if DirAccess.dir_exists_absolute(candidate):
			return candidate
	return str(dirs[0]).path_join(relative)


## A folder under `steamapps/common`, e.g. "Half-Life/valve".
static func common(relative: String) -> String:
	return steamapps("common".path_join(relative))


static func installed(path: String) -> bool:
	return DirAccess.dir_exists_absolute(path)
