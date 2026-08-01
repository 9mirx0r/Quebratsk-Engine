extends Node

## How much of a model was being left out without anybody knowing?
##
## Only the first version of each body part is built, which is what the game does at any given
## moment. The problem was never that choice, it was the silence: a scientist has four heads
## and a weapon has a magazine that can be absent, and an importer that takes the first and
## says nothing leaves somebody hunting for a variant that was there all along.
##
## Attachments are the same shape of omission. A muzzle flash belongs at a named point on a
## bone, and nothing else in the file says where.

var _vfs: VFSManager
var _importer: UnifiedAssetImporter


func _ready() -> void:
	_vfs = VFSManager.new()
	_importer = UnifiedAssetImporter.new()
	add_child(_vfs); add_child(_importer); _importer.set_vfs(_vfs)
	var games: Dictionary = SteamLibraryDetector.detect_installed_games()
	var n := 0
	for t in games:
		var root := str(games[t])
		for a in _vfs.scan_game_directory(root).get("archives", []):
			if _vfs.mount_container("g%d" % n, str(a)): n += 1
		_vfs.mount_directory("l%d" % n, root); n += 1

	print("\n=== BODY GROUPS AND ATTACHMENTS ===")
	# Weapon view models and player models, which is where these live. A flat scan of models/
	# comes back alphabetical and almost entirely Source, and reported that nothing anywhere
	# has a body group.
	var files := PackedStringArray()
	for needle in ["models/v_", "models/p_", "models/player/"]:
		var found: Dictionary = _vfs.find_files(needle, PackedStringArray(["mdl"]),
			PackedStringArray(["vvd", "vtx", "ani", "phy"]), PackedStringArray(), 60)
		for f in found["files"]:
			files.append(str(f))
	var hit := {"files": files}

	var looked := 0
	var with_groups := 0
	var hidden := 0
	var with_points := 0
	var shown := 0

	for entry in hit["files"]:
		if looked >= 40: break
		var uri := str(entry)
		# GoldSrc only: a Source model goes through a different parser that does not read
		# either of these, and the first draft of this scanned a sample that was almost all
		# Source and reported that nothing anywhere has a body group.
		var head := _vfs.read_file(uri)
		if head.size() < 8 or head.decode_s32(4) != 10:
			continue
		looked += 1

		var groups: Array = _importer.list_body_groups(uri)
		var points: Array = _importer.list_attachments(uri)
		if groups.is_empty() and points.is_empty():
			continue

		if not groups.is_empty():
			with_groups += 1
			for g in groups:
				# Everything past the one that was built is geometry nobody could reach.
				hidden += maxi(int((g as Dictionary)["options"].size()) - 1, 0)
		if not points.is_empty():
			with_points += 1

		if shown < 5:
			shown += 1
			print("\n%s" % uri.get_file())
			for g in groups:
				var d: Dictionary = g
				var options: PackedStringArray = d["options"]
				print("   part '%s': %d versions, built #%d  [%s]"
					% [d["name"], options.size(), d["chosen"],
					   ", ".join(options.slice(0, 4))])
			if not points.is_empty():
				var named := PackedStringArray()
				for p in points.slice(0, 4):
					named.append(str((p as Dictionary)["name"]))
				print("   %d attachment(s): %s" % [points.size(), ", ".join(named)])

	print("\n=== SUMMARY ===")
	print("  %d models read" % looked)
	print("  %d have a part with alternatives, hiding %d pieces between them"
		% [with_groups, hidden])
	print("  %d carry attachment points" % with_points)
	get_tree().quit()
