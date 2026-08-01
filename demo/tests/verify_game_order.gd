extends Node

## Does a lookup answer with the asking game's file?
##
## A bare reference like "models/weapons/v_knife.mdl" is answered by searching the index, and
## with a dozen games mounted many of them hold a file by that name. The index is a hash map,
## so the old answer was whichever one the bucket layout reached first: the same reference
## could resolve into Half-Life 2 for a Counter-Strike model, and there was no way to tell
## from the outside that anything was wrong. The model just quietly wore the wrong texture.
##
## The check that means something is the one below: find paths that exist in more than one
## game, then ask for each of them once per game and see whether the answer follows the asker.

var _vfs: VFSManager


func _ready() -> void:
	_vfs = VFSManager.new()
	add_child(_vfs)

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

	print("\n=== GAME CONTENT ORDER ===")
	_report_manifests()
	_report_contested()
	get_tree().quit()


## What each game says it draws on. A game with no manifest reports nothing, which is not a
## failure: a map bundle sitting in Downloads belongs to no game and should say so.
func _report_manifests() -> void:
	var order: Dictionary = _vfs.get_game_search_order()
	print("\nmanifests read: %d game(s)" % order.size())
	var dirs := order.keys()
	dirs.sort()
	for dir in dirs:
		var chain: PackedStringArray = (order[dir] as Dictionary)["falls_back_to"]
		# Two games can share a folder name, so show enough of the path to tell them apart.
		var where := "/".join(str(dir).split("/").slice(-2))
		if chain.is_empty():
			print("   %-34s draws on nothing else" % where)
		else:
			print("   %-34s falls back to %s" % [where, ", ".join(chain)])


## Answering from a game the asker falls back to is right, not a miss: that is what a fallback
## is for. Half-Life 2 content reaching an Episode Two model is the intended answer.
func _fallbacks_of(game: String) -> PackedStringArray:
	var order: Dictionary = _vfs.get_game_search_order()
	if not order.has(game):
		return PackedStringArray()
	# The chain is reported by display name and the answer comes back as a directory.
	var names: PackedStringArray = (order[game] as Dictionary)["falls_back_to"]
	var dirs := PackedStringArray()
	for other in order:
		if str((order[other] as Dictionary)["name"]) in names:
			dirs.append(str(other))
	return dirs


## The real check: a path that several games hold a copy of.
func _report_contested() -> void:
	# Anything common enough to collide will do; weapons and player models are where the
	# games overlap most, which is also where the wrong answer was most visible.
	var seen := {}          # tail path -> { game: full uri }
	for needle in ["models/weapons/", "models/player/", "sound/weapons/"]:
		var hit: Dictionary = _vfs.find_files(needle, PackedStringArray(),
			PackedStringArray(), PackedStringArray(), 0)
		for entry in hit["files"]:
			var uri := str(entry)
			var game := _vfs.get_game_of(uri)
			if game.is_empty():
				continue
			# The path as a reference would name it, which means without the mount and
			# without the game folder. A loose GoldSrc file is indexed as
			# "cstrike/models/player/..." and its Condition Zero twin as "czero/models/...",
			# so leaving the game in front means no two copies ever look like the same path
			# and nothing is ever found to be contested.
			var tail := uri.substr(uri.find("/", 6) + 1)
			var folder := game.get_file() + "/"
			if tail.begins_with(folder):
				tail = tail.substr(folder.length())
			if not seen.has(tail):
				seen[tail] = {}
			(seen[tail] as Dictionary)[game] = uri

	var contested := []
	for tail in seen:
		if (seen[tail] as Dictionary).size() > 1:
			contested.append(tail)
	contested.sort()

	print("\n%d path(s) exist in more than one game" % contested.size())
	if contested.is_empty():
		print("   nothing to check: only one game holds each of these")
		return

	var correct := 0
	var wrong := 0
	var shown := 0

	for tail in contested:
		var owners: Dictionary = seen[tail]
		for game in owners:
			# Ask on behalf of the copy that lives in this game. The right answer is that
			# copy, or at worst a copy from a game this one falls back to.
			var origin := str(owners[game])
			var answer := _vfs.resolve_reference(tail, origin)
			var answer_game := _vfs.get_game_of(answer)

			if answer_game == game or answer_game in _fallbacks_of(game):
				correct += 1
			else:
				wrong += 1
				if shown < 6:
					shown += 1
					print("   %s asked as %s, answered from %s"
						% [tail, game.get_file(), answer_game.get_file()])

	print("\n   %d of %d lookups answered from the asking game" % [correct, correct + wrong])
	if wrong == 0:
		print("   every contested path follows its asker")
	else:
		print("   ** %d lookups crossed into another game **" % wrong)
