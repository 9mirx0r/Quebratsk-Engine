extends Node

## Do the sounds the game's code names actually exist in the installed game?
##
## `tools/build_sound_catalogue.py` reads 313 sound names out of ReGameDLL_CS. Every one of
## them is a claim: that Counter-Strike plays `weapons/c4_beep1.wav` when the bomb is counting
## down. A claim that reads well and resolves to nothing gives a bomb that ticks in silence,
## and nothing about the code would look wrong.
##
## So each name is looked up in the installed game and the ones that are not there are named.
## A miss is not automatically a defect: ReGameDLL_CS supports mods and features that retail
## Counter-Strike 1.6 does not ship files for. Knowing which is the point.

const SoundCatalogue := preload("res://addons/quebratsk_editor/sound_catalogue.gd")
const REPORT := "user://sounds_resolve.txt"
const ROOT := "C:/Program Files (x86)/Steam/steamapps/common/Half-Life/cstrike"

var _vfs: VFSManager
var _report := PackedStringArray()


func _say(line: String) -> void:
	print(line)
	_report.append(line)


func _ready() -> void:
	_vfs = VFSManager.new()
	add_child(_vfs)

	if not DirAccess.dir_exists_absolute(ROOT):
		_say("cstrike is not installed at %s" % ROOT)
		get_tree().quit()
		return
	# The loose tree is enough: GoldSrc keeps its sounds as plain .wav files under sound/, not
	# inside an archive.
	_vfs.mount_directory("cs_loose", ROOT)
	# Half-Life's, because Counter-Strike falls back to it and several of these live there.
	var valve := ROOT.get_base_dir().path_join("valve")
	if DirAccess.dir_exists_absolute(valve):
		_vfs.mount_directory("valve_loose", valve)

	_say("=== SOUNDS THE GAME'S CODE NAMES ===")
	_say("%-18s %5s %5s   %s" % ["group", "named", "found", "missing"])

	var named := 0
	var found := 0
	for label in SoundCatalogue.groups():
		var names := SoundCatalogue.group(str(label))
		var here := 0
		var absent := PackedStringArray()
		for entry in names:
			named += 1
			if _exists(str(entry)):
				here += 1
				found += 1
			else:
				absent.append(str(entry).get_file())
		_say("%-18s %5d %5d   %s" % [label, names.size(), here,
			"-" if absent.is_empty() else ", ".join(absent.slice(0, 6))
				+ ("" if absent.size() <= 6 else " and %d more" % (absent.size() - 6))])

	_say("\n=== SUMMARY ===")
	_say("  %d names, %d present in this install (%d%%)"
		% [named, found, 0 if named == 0 else roundi(100.0 * found / named)])

	# The ones a level cannot do without. A missing footstep is a character that walks in
	# silence; a missing c4_beep is a bomb nobody can hear counting down.
	_say("\n  the ones that matter most:")
	for label in ["footstep", "pain", "death", "c4_beep", "c4_plant", "c4_explode",
			"grenade_bounce", "grenade_explode", "landing"]:
		var names := SoundCatalogue.group(label)
		var here := 0
		for entry in names:
			if _exists(str(entry)):
				here += 1
		_say("    %-18s %d of %d" % [label, here, names.size()])

	var file := FileAccess.open(REPORT, FileAccess.WRITE)
	if file != null:
		file.store_string("\n".join(_report) + "\n")
		file.close()
	get_tree().quit()


## Is this sound in the install? The catalogue writes names the way the game does, relative to
## sound/, so that prefix goes on here.
func _exists(name: String) -> bool:
	var hit: Dictionary = _vfs.find_files("sound/" + name, PackedStringArray(["wav"]),
		PackedStringArray(), PackedStringArray(), 1)
	return not (hit["files"] as PackedStringArray).is_empty()
