extends Node

## What does a map say about itself, and does each game say it differently?
##
## Every engine on this list writes its entities into the same lump in the same syntax, and
## then uses completely different classnames inside it. Counter-Strike has one entity for
## where counter-terrorists start and another for terrorists; Half-Life has one start point
## for one player; Half-Life 2 has neither in most of its maps because it spawns you from a
## save. Anything built on top of this has to know which it is looking at, so this counts
## what is actually there rather than assuming.

var _vfs: VFSManager
var _importer: UnifiedAssetImporter


func _ready() -> void:
	_vfs = VFSManager.new()
	_importer = UnifiedAssetImporter.new()
	add_child(_vfs)
	add_child(_importer)
	_importer.set_vfs(_vfs)

	var games: Dictionary = SteamLibraryDetector.detect_installed_games()
	print("\n=== MAP ENTITY SURVEY ===")

	var n := 0
	for title in games:
		var root := str(games[title])
		# One game at a time, mounted on its own, so a map can be attributed to the game it
		# came from rather than to whichever mount happened to hold it.
		var solo := VFSManager.new()
		add_child(solo)
		solo.mount_directory("m%d" % n, root)
		n += 1

		var probe := UnifiedAssetImporter.new()
		add_child(probe)
		probe.set_vfs(solo)
		_survey(str(title), solo, probe)
		probe.queue_free()
		solo.queue_free()

	print("\n=== DONE ===")
	get_tree().quit()


func _survey(title: String, vfs: VFSManager, importer: UnifiedAssetImporter) -> void:
	var hit: Dictionary = vfs.find_files("maps/", PackedStringArray(["bsp"]),
		PackedStringArray(), PackedStringArray(), 0)
	var maps: PackedStringArray = hit["files"]
	if maps.is_empty():
		return

	var classes := {}
	var read := 0
	var spawn_counts := {}

	for entry in maps:
		if read >= 12:
			break
		var entities: Array = importer.load_map_entities(str(entry))
		if entities.is_empty():
			continue
		read += 1

		var here := {}
		for e in entities:
			var ent: Dictionary = e
			var cls := str(ent.get("classname", ""))
			if cls.is_empty():
				continue
			classes[cls] = int(classes.get(cls, 0)) + 1
			if cls.contains("player") and cls.contains("info"):
				here[cls] = int(here.get(cls, 0)) + 1

		for cls in here:
			if not spawn_counts.has(cls):
				spawn_counts[cls] = []
			spawn_counts[cls].append(here[cls])

	if read == 0:
		return

	print("\n%s: %d of %d maps read" % [title, read, maps.size()])

	# Where players begin. This is the whole point of the exercise: something dropped into
	# a level has to go where the level says people go.
	if spawn_counts.is_empty():
		print("   no player-start entities of any kind")
	else:
		for cls in spawn_counts:
			var counts: Array = spawn_counts[cls]
			var total := 0
			for c in counts:
				total += int(c)
			print("   %-34s in %d map(s), %d per map on average"
				% [cls, counts.size(), int(round(float(total) / counts.size()))])

	# The rest, most common first, as a picture of what else a map is made of.
	var names := classes.keys()
	names.sort_custom(func(a, b): return classes[a] > classes[b])
	var line := "   most common: "
	for i in mini(6, names.size()):
		line += "%s (%d)  " % [names[i], classes[names[i]]]
	print(line)
	print("   %d distinct entity types in total" % names.size())
