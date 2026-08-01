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
	{"role": "crouch", "hints": ["crouch_idle_all", "crouch_idle", "crouch_all", "cidle_all",
		"cidle", "crouch1", "crouch", "duck"]},
	# Counter-Strike calls crouched movement "crouchrun" and has no crouched walk at all, so a
	# character that could stand, walk and crouch still slid across the floor the moment it
	# crouched and moved. Read out of the game's own models rather than guessed: see
	# demo/tests/probe_cs16_sequences.gd.
	{"role": "crouch_walk", "hints": ["crouch_walk_all", "crouch_walk", "crouchwalk",
		"crouchrun", "crouch_run", "cwalk"]},
	{"role": "jump",   "hints": ["jump_all", "jump1", "jump", "leap"]},
	{"role": "shoot",  "hints": ["shoot1", "shoot", "fire1", "fire", "attack1", "attack"]},
	{"role": "reload", "hints": ["reload1", "reload"]},
	{"role": "die",    "hints": ["die_simple", "death1", "death", "die1", "die", "dead"]},
]

## Roles worth falling back on when a model has nothing for the one asked for.
##
## A stance that is missing is better answered by a related one than by nothing: a character
## with no crouched walk should shuffle in its crouched idle rather than glide. The chain ends
## at idle, which every character has.
const FALLBACKS := {
	"run": ["walk", "idle"],
	"walk": ["run", "idle"],
	"crouch_walk": ["crouch", "walk", "idle"],
	"crouch": ["idle"],
	"jump": ["run", "idle"],
}


## The first of `role` and its fallbacks that `moves` actually has.
##
## Returns "" when even idle is missing, which means the model carries no stance this code
## knows how to name and the caller should leave the skeleton alone.
static func best(moves: Dictionary, role: String) -> String:
	if moves.has(role):
		return str(moves[role])
	for alternative in FALLBACKS.get(role, ["idle"]):
		if moves.has(alternative):
			return str(moves[alternative])
	return str(moves.get("idle", ""))


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


# ---------------------------------------------------------------------- weapons ----
#
# A view model is not a character and its sequences are named nothing like one's. Read out of
# all thirty-one Counter-Strike 1.6 view models rather than assumed: see
# demo/tests/probe_cs16_sequences.gd, and demo/tests/verify_weapon_actions.gd for the check
# that every one of them resolves.

## The words a weapon uses for being fired, swung or thrust.
const ATTACK_HEADS := ["shoot", "fire", "attack", "slash", "midslash", "stab"]

## Endings that name a special case rather than an ordinary attack, and playing one at random
## makes a working weapon look broken. shoot_empty and shootlast are the last round leaving the
## magazine, stab_miss is a swing that connects with nothing, and the _unsil variants belong to
## the other version of a weapon that carries a silencer.
const NOT_ORDINARY := ["_unsil", "_empty", "last", "_miss"]


## Which of a weapon's sequences are it being used on somebody.
##
## "shoot" is not the only word for it. A knife has slash1, midslash1, stab and stab_miss and
## no shoot at all, which is why a knife swung motionlessly.
static func attacks(has: PackedStringArray) -> PackedStringArray:
	var out := PackedStringArray()
	for sequence in has:
		var lowered := str(sequence).to_lower()
		var ordinary := true
		for ending in NOT_ORDINARY:
			if lowered.ends_with(str(ending)):
				ordinary = false
				break
		if not ordinary:
			continue
		for head in ATTACK_HEADS:
			if lowered.begins_with(str(head)):
				out.append(str(sequence))
				break
	return out


## How a weapon reloads, as the sequences to play in order.
##
## Most have one called reload. The pump shotguns do not: the M3 and the XM1014 load a shell at
## a time as start_reload, then insert once per shell, then after_reload. Looking only for
## "reload" found nothing on those two and left the reload key doing nothing at all.
##
## Returns an empty array for a weapon that does not reload, which is every grenade, the knife
## and the C4.
static func reload_chain(has: PackedStringArray, shells := 4) -> PackedStringArray:
	var by_name := {}
	for sequence in has:
		by_name[str(sequence).to_lower()] = str(sequence)

	for plain in ["reload", "reload1"]:
		if by_name.has(plain):
			return PackedStringArray([str(by_name[plain])])

	if by_name.has("start_reload") and by_name.has("insert"):
		var chain := PackedStringArray([str(by_name["start_reload"])])
		for i in shells:
			chain.append(str(by_name["insert"]))
		if by_name.has("after_reload"):
			chain.append(str(by_name["after_reload"]))
		return chain
	return PackedStringArray()
