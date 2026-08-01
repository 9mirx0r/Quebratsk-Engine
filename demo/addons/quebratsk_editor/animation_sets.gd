@tool
extends RefCounted

## Which of a model's sequences are the ones a character actually needs.
##
## A Condition Zero player model carries 494 of them. Almost all are situational: sitting in
## an APC, flinching from a hit in the left shin, a scripted turn for one level. What a
## character has to be able to do in any game is a short list, and picking it out of the long
## one is guesswork about names rather than anything the file says.
##
## Shared rather than written where it is needed, because it is exactly the kind of knowledge
## that drifts: the dock and the sandbox both need it, and two copies would disagree the first
## time either learned about a naming convention the other did not.

## The roles worth importing, in the order a character needs them, and the names each engine
## and each modeller has used for them. Matched on the front of the name, most specific first,
## so "idle_all_01" is preferred over anything merely starting with "idle".
const ROLES := [
	{"role": "idle",   "hints": ["idle_all_01", "idle_all", "idle_subtle", "idle1", "idle"]},
	{"role": "walk",   "hints": ["walk_all", "walkall", "walk1", "walk"]},
	{"role": "run",    "hints": ["run_all", "runall", "run1", "run", "sprint"]},
	# Source abbreviates the crouched variant of a stance by putting a c in front of it, so
	# the crouched idle_all is cidle_all. Without that, no Source model reports a crouch.
	{"role": "crouch", "hints": ["crouch_idle_all", "crouch_all", "cidle_all", "cidle",
		"crouch1", "crouch", "duck"]},
	{"role": "jump",   "hints": ["jump_all", "jump1", "jump", "leap"]},
	{"role": "shoot",  "hints": ["shoot1", "shoot", "fire1", "fire", "attack1", "attack"]},
	{"role": "reload", "hints": ["reload1", "reload"]},
	{"role": "die",    "hints": ["die_simple", "death1", "death", "die1", "die", "dead"]},
]


## The best sequence for each role a model has one for, as { role: sequence name }.
##
## A role with nothing matching is left out rather than filled with something approximate: a
## model with no death animation is better described as having none than as dying by standing
## still under a name that says otherwise.
static func usual_moves(poses: PackedStringArray, weapon_extension := "") -> Dictionary:
	# Counter-Strike does not have one walk. It has one per weapon, named by suffix, so a
	# player carrying an AK plays walk_ak47 and one holding a knife plays walk_knife. They
	# differ: the arms are in a different place and the body leans differently. Taking the
	# unsuffixed sequence gives everybody the same neutral stance no matter what they hold,
	# which is why every character in the sandbox ran identically with a rifle and a knife.
	#
	# The suffix comes from the game's own weapon table, not from the filename.
	if not weapon_extension.is_empty():
		var armed := _match_roles(poses, "_" + weapon_extension.to_lower())
		if armed.size() >= 2:
			return armed
	return _match_roles(poses, "")


static func _match_roles(poses: PackedStringArray, suffix: String) -> Dictionary:
	var lowered := PackedStringArray()
	for p in poses:
		lowered.append(str(p).to_lower())

	var chosen := {}
	var taken := {}
	for entry in ROLES:
		var role := str((entry as Dictionary)["role"])
		for hint in (entry as Dictionary)["hints"]:
			var found := -1
			for i in lowered.size():
				# With a suffix asked for, only sequences carrying it count.
				if not suffix.is_empty() and not lowered[i].ends_with(suffix):
					continue
				# One sequence cannot stand for two roles. Without this, a model whose only
				# match for "run" is the same file that already answered "walk" imports one
				# animation under two names and looks like it has both.
				if taken.has(i):
					continue
				if lowered[i].begins_with(str(hint)):
					found = i
					break
			if found >= 0:
				chosen[role] = str(poses[found])
				taken[found] = true
				break
	return chosen


## Just the sequence names, in role order, for handing straight to load_model().
static func usual_move_names(poses: PackedStringArray, weapon_extension := "") -> PackedStringArray:
	var moves := usual_moves(poses, weapon_extension)
	var out := PackedStringArray()
	for entry in ROLES:
		var role := str((entry as Dictionary)["role"])
		if moves.has(role):
			out.append(str(moves[role]))
	return out
