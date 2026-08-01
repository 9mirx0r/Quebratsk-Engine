extends Node

## Do a map's own sounds resolve to files?
##
## A level is not only geometry. It declares where the generator hums, where the wind blows,
## where the water runs, as ambient_generic entities carrying a sound name and a position.
## Those were read off disk and thrown away, so every imported map was silent.
##
## The name in the entity is not a path in either engine. GoldSrc writes it relative to
## sound/, Source often writes a soundscript entry instead. This counts how many a map has and
## how many of them reach a real file, because a map that declares fifteen sounds and can play
## two is a different problem from one that declares none.

var _vfs: VFSManager
var _importer: UnifiedAssetImporter


func _ready() -> void:
	_vfs = VFSManager.new()
	_importer = UnifiedAssetImporter.new()
	add_child(_vfs)
	add_child(_importer)
	_importer.set_vfs(_vfs)

	# The downloaded map bundle first, since it brings its own sounds.
	var bundle := "C:/Users/emir/Downloads/de_caminito"
	var bundle_scan: Dictionary = _vfs.scan_game_directory(bundle)
	var b := 0
	for archive in bundle_scan.get("archives", []):
		if _vfs.mount_container("dlw%d" % b, str(archive)):
			b += 1
	_vfs.mount_directory("dl", bundle)

	var games: Dictionary = SteamLibraryDetector.detect_installed_games()
	var n := 0
	for title in games:
		var root := str(games[title])
		var scan: Dictionary = _vfs.scan_game_directory(root)
		for archive in scan.get("archives", []):
			if _vfs.mount_container("g%d" % n, str(archive)):
				n += 1
		_vfs.mount_directory("loose%d" % n, root)
		n += 1

	print("\n=== MAP AMBIENT SOUND ===")

	var hit: Dictionary = _vfs.find_files("maps/", PackedStringArray(["bsp"]),
		PackedStringArray(), PackedStringArray(), 0)

	var checked := 0
	var declared := 0
	var resolved := 0

	for entry in hit["files"]:
		if checked >= 6:
			break
		var result: Dictionary = _check(str(entry))
		if result.is_empty():
			continue
		checked += 1
		declared += int(result["declared"])
		resolved += int(result["resolved"])

	print("\n=== SUMMARY ===")
	print("  %d maps with ambient sound" % checked)
	print("  %d sounds declared, %d of them reach a file" % [declared, resolved])
	if declared > 0 and resolved == 0:
		print("  ** nothing a map asks for can be played **")
	get_tree().quit()


func _check(uri: String) -> Dictionary:
	var entities: Array = _importer.load_map_entities(uri)
	if entities.is_empty():
		return {}

	var ambient := []
	for e in entities:
		var entity: Dictionary = e
		if str(entity.get("classname", "")) == "ambient_generic":
			ambient.append(entity)
	if ambient.is_empty():
		return {}

	print("\n%s: %d ambient_generic" % [uri.get_file(), ambient.size()])

	# What keys these actually carry, rather than what they are documented to carry. Maps are
	# hand-authored and a key the mapper never set simply is not there.
	var keys := {}
	for entity in ambient:
		for k in (entity as Dictionary):
			keys[k] = int(keys.get(k, 0)) + 1
	var key_names := keys.keys()
	key_names.sort()
	var summary := []
	for k in key_names:
		summary.append("%s(%d)" % [k, keys[k]])
	print("   keys: %s" % ", ".join(PackedStringArray(summary)))

	var found := 0
	var shown := 0
	for entity in ambient:
		var message := str((entity as Dictionary).get("message", ""))
		if message.is_empty():
			continue
		var files: PackedStringArray = _importer.resolve_sound(message, uri)
		if files.is_empty():
			if shown < 4:
				shown += 1
				print("   not found: %s" % message)
		else:
			found += 1
			if found <= 2:
				print("   %s -> %s" % [message, str(files[0])])

	print("   %d of %d reach a file" % [found, ambient.size()])
	return {"declared": ambient.size(), "resolved": found}
