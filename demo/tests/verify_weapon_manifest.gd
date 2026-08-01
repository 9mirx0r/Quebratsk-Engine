extends Node

## Does what the game says about a weapon reach a file on this machine?
##
## The manifest is facts read out of Counter-Strike's own code: which view model pairs with
## which world model, which sound each weapon fires. None of that is worth anything if the
## paths it names cannot be found in the archives actually mounted, and a manifest that looks
## complete while resolving to nothing is worse than none, because it reads as an answer.

const WeaponManifest := preload("res://addons/quebratsk_editor/weapon_manifest.gd")

var _vfs: VFSManager


func _ready() -> void:
	_vfs = VFSManager.new()
	add_child(_vfs)
	var games: Dictionary = SteamLibraryDetector.detect_installed_games()
	var n := 0
	for t in games:
		var root := str(games[t])
		for a in _vfs.scan_game_directory(root).get("archives", []):
			if _vfs.mount_container("g%d" % n, str(a)):
				n += 1
		_vfs.mount_directory("l%d" % n, root)
		n += 1

	print("\n=== WEAPON MANIFEST ===")
	var file := FileAccess.open(WeaponManifest.PATH, FileAccess.READ)
	if file == null:
		print("no manifest at %s" % WeaponManifest.PATH)
		get_tree().quit()
		return
	var data: Dictionary = JSON.parse_string(file.get_as_text())
	file.close()
	var weapons: Dictionary = data["weapons"]

	var views := 0
	var view_found := 0
	var fires := 0
	var fire_found := 0
	var speeds := 0
	var missing := []

	for name in weapons:
		var entry: Dictionary = weapons[name]

		if entry.has("view_model"):
			views += 1
			if not _vfs.resolve_reference("/" + str(entry["view_model"])).is_empty():
				view_found += 1
			elif missing.size() < 6:
				missing.append(str(entry["view_model"]))

		var shots: PackedStringArray = WeaponManifest.fire_sounds(str(name))
		if not shots.is_empty():
			fires += 1
			for s in shots:
				if not _vfs.resolve_reference("/" + str(s)).is_empty():
					fire_found += 1
					break

		if WeaponManifest.max_speed(str(name)) > 0.0:
			speeds += 1

	print("  %d weapons declared" % weapons.size())
	print("  %d of %d view models found in the mounted games" % [view_found, views])
	print("  %d of %d firing sounds found" % [fire_found, fires])
	print("  %d carry a max speed" % speeds)
	if not missing.is_empty():
		print("  not on this machine: %s" % ", ".join(PackedStringArray(missing)))

	# The lookup has to work from any of a weapon's faces, which is the point of it.
	print("\n  reaching one weapon by each of its names:")
	for probe in ["v_ak47.mdl", "p_ak47.mdl", "w_ak47.mdl", "ak47"]:
		var e := WeaponManifest.lookup(probe)
		print("   %-14s -> %s, %d damage, %.2f m/s"
			% [probe, e.get("view_model", "nothing"), int(e.get("damage", 0)),
			   WeaponManifest.max_speed(probe)])
	get_tree().quit()
