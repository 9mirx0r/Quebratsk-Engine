extends Node

## Write a preset for every character and weapon in Counter-Strike 1.6.
##
## A preset is what a model needs in order to act rather than merely to be drawn: which of its
## sequences is standing and which is running, which of its parts have a second version, what
## it sounds like, how fast the weapon in its hands lets it move. All of that is derived from
## the game here and written to res://presets, so placing a guerilla in a scene gives you a
## guerilla and not a mesh.
##
## Run it again after changing anything that decides those answers, and read the diff. That is
## the point of writing them down: a change in how a stance is chosen becomes visible as a line
## that moved rather than as a character that behaves differently for no stated reason.

const ModelPreset := preload("res://addons/quebratsk_editor/model_preset.gd")
const REPORT := "user://presets_built.txt"

const GAME := "Counter-Strike 1.6"
const ROOT := "C:/Program Files (x86)/Steam/steamapps/common/Half-Life/cstrike"

var _vfs: VFSManager
var _importer: UnifiedAssetImporter
var _report := PackedStringArray()


func _say(line: String) -> void:
	print(line)
	_report.append(line)


func _ready() -> void:
	_vfs = VFSManager.new()
	_importer = UnifiedAssetImporter.new()
	add_child(_vfs); add_child(_importer); _importer.set_vfs(_vfs)

	if not DirAccess.dir_exists_absolute(ROOT):
		_say("cstrike is not installed at %s" % ROOT)
		get_tree().quit()
		return
	var n := 0
	for a in _vfs.scan_game_directory(ROOT).get("archives", []):
		if _vfs.mount_container("cs%d" % n, str(a)): n += 1
	_vfs.mount_directory("cs_loose", ROOT)

	_say("=== BUILDING PRESETS FOR %s ===" % GAME)

	# Weapons first: a character's preset carries the weapon it holds, and holding one decides
	# how fast the character moves.
	var weapons := _build_all("models/v_", "weapon")
	var people := _build_all("models/player/", "character")

	_say("\n=== SUMMARY ===")
	_say("  %d weapon preset(s)" % weapons)
	_say("  %d character preset(s)" % people)
	_say("  written to %s" % ProjectSettings.globalize_path(ModelPreset.FOLDER))

	var file := FileAccess.open(REPORT, FileAccess.WRITE)
	if file != null:
		file.store_string("\n".join(_report) + "\n")
		file.close()
	get_tree().quit()


func _build_all(needle: String, expect: String) -> int:
	_say("\n--- %ss ---" % expect)
	var hit: Dictionary = _vfs.find_files(needle, PackedStringArray(["mdl"]),
		PackedStringArray(["vvd", "vtx", "ani", "phy"]), PackedStringArray(), 0)

	var written := 0
	for entry in hit["files"]:
		var uri := str(entry)
		var preset: Dictionary = ModelPreset.build(_importer, uri)
		# A model that turned out to be scenery is not worth a preset. Reported rather than
		# skipped in silence, because a character landing here means the test that separates
		# people from props is wrong again.
		if str(preset.get("kind", "")) != expect:
			_say("   %-22s is a %s, skipped" % [uri.get_file(), preset.get("kind", "?")])
			continue

		if ModelPreset.save(preset, GAME).is_empty():
			continue
		written += 1
		_say("   %-22s %s" % [uri.get_file(), _describe(preset)])
	return written


## One line saying what the preset actually captured, so an empty one is visible.
func _describe(preset: Dictionary) -> String:
	if str(preset["kind"]) == "weapon":
		var attacks: Array = preset.get("attacks", [])
		var reload: Array = preset.get("reload", [])
		return "%d attack(s), %s, %d sound(s)" % [attacks.size(),
			"reloads in %d step(s)" % reload.size() if not reload.is_empty() else "no reload",
			(preset.get("fire_sounds", []) as Array).size()]

	var stances: Dictionary = preset.get("stances", {})
	var parts: Dictionary = preset.get("parts", {})
	return "%d stance(s): %s%s" % [stances.size(), ", ".join(PackedStringArray(stances.keys())),
		"" if parts.is_empty() else "   parts: " + ", ".join(PackedStringArray(parts.keys()))]
