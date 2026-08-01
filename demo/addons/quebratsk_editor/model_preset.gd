@tool
extends RefCounted

## Everything a model needs in order to act, worked out once and written down.
##
## Importing a model gives you geometry and a skeleton. What it does not give you is the model
## being a character: which of its hundred and eleven sequences is standing and which is
## running, which of its parts have a second version, which sounds it makes, how fast the
## weapon in its hands lets it move, how quickly that weapon fires. All of that is a property
## of the game rather than of the file, and every caller was working it out again from scratch
## — the sandbox one way, the dock another.
##
## A preset is that answer, derived from the game and saved as JSON. Derived rather than
## authored on purpose: a hand-written table is a claim about the game, and the whole point of
## this project is that claims about these games are wrong more often than not. Anything here
## can be read, corrected and committed, and the builder can be run again to see what changed.
##
## Presets live in res://presets/<game>/<model>.json.

const AnimationSets := preload("res://addons/quebratsk_editor/animation_sets.gd")
const WeaponManifest := preload("res://addons/quebratsk_editor/weapon_manifest.gd")

## Bumped when the shape below changes, so an old file on disk is recognised rather than
## silently misread.
const VERSION := 1
const FOLDER := "res://presets"


## Work out everything about a model, by asking the importer and the game's own tables.
##
## `weapon_uri` is the view model a character is holding, and it matters: what you carry
## decides how fast you move in Counter-Strike, and the character's own file says nothing
## about it.
static func build(importer, uri: String, weapon_uri := "") -> Dictionary:
	var poses: PackedStringArray = importer.list_poses(uri)
	var preset := {
		"quebratsk_preset": VERSION,
		"name": uri.get_file().get_basename(),
		"model": engine_path(uri),
		"kind": _kind_of(uri, poses),
		"sequences": poses.size(),
	}

	# Which of its parts come in more than one version, and which one is built. Recorded even
	# when the answer is the default, because the point of a preset is that the choice is
	# visible and editable rather than implicit.
	var parts := {}
	for group in importer.list_body_groups(uri):
		var described: Dictionary = group
		parts[str(described["name"])] = {
			"chosen": int(described["chosen"]),
			"options": Array(described["options"] as PackedStringArray),
		}
	if not parts.is_empty():
		preset["parts"] = parts

	var points := PackedStringArray()
	for attachment in importer.list_attachments(uri):
		points.append(str((attachment as Dictionary)["name"]))
	if not points.is_empty():
		preset["attachments"] = Array(points)

	if preset["kind"] == "weapon":
		_describe_weapon(importer, uri, poses, preset)
	else:
		_describe_character(importer, uri, poses, weapon_uri, preset)
	return preset


## A weapon: what it does when the trigger goes, and what it costs to carry.
static func _describe_weapon(importer, uri: String, poses: PackedStringArray,
		into: Dictionary) -> void:
	var bare := uri.get_file().get_basename()
	into["attacks"] = Array(AnimationSets.attacks(poses))
	into["reload"] = Array(AnimationSets.reload_chain(poses))

	# From the game's own weapon table rather than from the model, because a model does not
	# know how fast it fires or how much it slows you down.
	var cycle: float = WeaponManifest.cycle_time(bare)
	if cycle > 0.0:
		into["cycle_time"] = cycle
	var speed: float = WeaponManifest.max_speed(bare)
	if speed > 0.0:
		into["max_speed"] = speed
	var animation_class: String = WeaponManifest.animation_extension(bare)
	if not animation_class.is_empty():
		# Which of a player model's ref_aim_* / ref_shoot_* sets belongs with this weapon.
		into["holder_class"] = animation_class

	var sounds := PackedStringArray()
	for declared in WeaponManifest.fire_sounds(uri):
		for found in importer.resolve_sound(str(declared), uri):
			sounds.append(engine_path(str(found)))
			break
	if sounds.is_empty():
		# What the model's own animation events name, which is the only other record of it.
		for found in importer.list_sounds(uri):
			sounds.append(engine_path(str(found)))
	if not sounds.is_empty():
		into["fire_sounds"] = Array(sounds)


## A character: how it stands, walks, crouches and dies, and what it carries.
static func _describe_character(importer, uri: String, poses: PackedStringArray,
		weapon_uri: String, into: Dictionary) -> void:
	var holder := ""
	if not weapon_uri.is_empty():
		holder = WeaponManifest.animation_extension(weapon_uri.get_file().get_basename())

	var stances: Dictionary = AnimationSets.usual_moves(poses, holder)
	into["stances"] = stances
	# Named so the omission is visible. Counter-Strike keeps the legs and the torso apart and
	# only the legs are chosen here; a caller that wants the arms aiming needs two layers.
	into["stance_layer"] = "legs"

	var sounds := PackedStringArray()
	for found in importer.list_sounds(uri):
		sounds.append(engine_path(str(found)))
	if not sounds.is_empty():
		into["sounds"] = Array(sounds)

	if not weapon_uri.is_empty():
		into["weapon"] = build(importer, weapon_uri)


## The path the game itself uses, with the mount stripped off.
##
## A preset records "models/player/guerilla/guerilla.mdl" and never
## "vfs://cs_loose/models/player/guerilla/guerilla.mdl". The mount name is a fact about the
## session that built the preset — how many archives had been opened first, in which order —
## and writing it down produces a file that works only in the project it was made in.
static func engine_path(uri: String) -> String:
	if not uri.begins_with("vfs://"):
		return uri
	var rest := uri.substr(6)
	var slash := rest.find("/")
	return rest if slash < 0 else rest.substr(slash + 1)


## The other direction: find what a preset names in whatever is mounted now.
static func resolve(vfs, path: String) -> String:
	if path.begins_with("vfs://"):
		return path
	var hit: Dictionary = vfs.find_files(path, PackedStringArray([path.get_extension()]),
		PackedStringArray(), PackedStringArray(), 1)
	var files: PackedStringArray = hit["files"]
	return "" if files.is_empty() else str(files[0])


## Is this a person, a weapon, or scenery?
##
## By what the file is for rather than by what it is called. A person carries hundreds of
## sequences because it has to stand, walk, crouch, reload and die; a vent grille carries one,
## and picking characters by folder alone once put Vine_wall2 in a level as an enemy.
static func _kind_of(uri: String, poses: PackedStringArray) -> String:
	var bare := uri.get_file().to_lower()
	if bare.begins_with("v_") or bare.begins_with("w_") or bare.begins_with("p_"):
		return "weapon"
	if poses.size() >= 20:
		return "character"
	return "prop"


# --------------------------------------------------------------------- on disk ----

## Where a preset for this model belongs. The game's name is part of the path because four
## games ship a model called arctic.mdl and they are not the same model.
static func path_for(preset: Dictionary, game := "") -> String:
	var folder := FOLDER if game.is_empty() else FOLDER.path_join(_slug(game))
	return folder.path_join(_slug(str(preset.get("name", "unnamed"))) + ".json")


static func save(preset: Dictionary, game := "") -> String:
	var path := path_for(preset, game)
	DirAccess.make_dir_recursive_absolute(path.get_base_dir())
	var file := FileAccess.open(path, FileAccess.WRITE)
	if file == null:
		push_warning("Could not write preset to %s" % path)
		return ""
	file.store_string(JSON.stringify(preset, "  ") + "\n")
	file.close()
	return path


## Read a preset back, by name or by full path.
##
## Returns an empty Dictionary when there is none, or when the file on disk was written by a
## version of this that arranged things differently: reading it as if it were current would
## produce a character missing whatever the shape has since gained.
static func find(name_or_path: String, game := "") -> Dictionary:
	var path := name_or_path
	if not path.ends_with(".json"):
		var folder := FOLDER if game.is_empty() else FOLDER.path_join(_slug(game))
		path = folder.path_join(_slug(name_or_path) + ".json")
	if not FileAccess.file_exists(path):
		return {}

	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		return {}
	var parsed = JSON.parse_string(file.get_as_text())
	file.close()
	if not (parsed is Dictionary):
		push_warning("Preset at %s is not readable." % path)
		return {}
	var preset: Dictionary = parsed
	if int(preset.get("quebratsk_preset", 0)) != VERSION:
		push_warning("Preset at %s was written by another version; rebuild it." % path)
		return {}
	return preset


## Every preset on disk, as { name: path }.
static func known(game := "") -> Dictionary:
	var folder := FOLDER if game.is_empty() else FOLDER.path_join(_slug(game))
	var out := {}
	if not DirAccess.dir_exists_absolute(folder):
		return out
	for entry in DirAccess.get_files_at(folder):
		var file := str(entry)
		if file.ends_with(".json"):
			out[file.get_basename()] = folder.path_join(file)
	return out


# ---------------------------------------------------------------------- using ----

## Build the model this preset describes, wired the way the preset says.
##
## Returns a CharacterBody3D for a character and a plain Node3D for anything else, with the
## preset itself left on the node as "quebratsk_preset" metadata so whatever drives it can
## read the stances and the sounds without being handed them separately.
##
## The behaviour is not included and that is deliberate: what an enemy does when it sees you
## is a decision about a game, and a preset is a record of what is in a file.
static func spawn(importer, vfs, preset: Dictionary) -> Node3D:
	if preset.is_empty():
		return null
	var uri := resolve(vfs, str(preset.get("model", "")))
	if uri.is_empty():
		push_warning("Preset '%s' names %s, which is not in anything mounted."
			% [preset.get("name", "?"), preset.get("model", "")])
		return null

	var stances: Dictionary = preset.get("stances", {})
	var sequences := PackedStringArray()
	for role in stances:
		sequences.append(str(stances[role]))
	for name in preset.get("attacks", []):
		sequences.append(str(name))
	for name in preset.get("reload", []):
		if not sequences.has(str(name)):
			sequences.append(str(name))

	var choices := {}
	for part in preset.get("parts", {}):
		choices[str(part)] = int((preset["parts"][part] as Dictionary).get("chosen", 0))

	var node: Node3D = null
	if str(preset.get("kind", "")) == "character":
		node = importer.load_character(uri, "", sequences, choices)
	else:
		node = importer.load_model(uri, "", sequences, choices)
	if node == null:
		return null

	node.name = str(preset.get("name", uri.get_file().get_basename()))
	node.set_meta("quebratsk_preset", preset)
	return node


static func _slug(text: String) -> String:
	var out := ""
	for i in text.length():
		var ch := text[i].to_lower()
		out += ch if (ch >= "a" and ch <= "z") or (ch >= "0" and ch <= "9") else "_"
	return out.lstrip("_").rstrip("_")
