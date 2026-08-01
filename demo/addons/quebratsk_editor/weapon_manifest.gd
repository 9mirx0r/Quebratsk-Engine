@tool
extends RefCounted

## What the game says about a weapon, rather than what its filename suggests.
##
## A model does not say which weapon it is. `v_ak47.mdl` carries a mesh, a skeleton and some
## sequences, and everything that makes it an AK rather than a shape lives in the game's code:
## which world model pairs with it, which sound it fires, how hard it hits, how much it slows
## you down.
##
## Quebratsk used to infer that. The rule was that `v_famas.mdl` fires `weapons/famas-1.wav`,
## which is true and is still a guess about a convention. This reads the answer instead, out of
## `data/cs_weapons.json`, generated from ReGameDLL_CS by `tools/build_weapon_manifest.py`.
##
## Absence is meaningful here. A knife has no clip size and the C4 makes no firing sound, and
## those fields are missing rather than zero, because a zero would read as a measurement.

const PATH := "res://addons/quebratsk_editor/data/cs_weapons.json"

static var _weapons: Dictionary = {}
static var _loaded := false


## Everything known about a weapon, by any of its model files or by its bare name.
##
## `v_ak47.mdl`, `p_ak47.mdl`, `w_ak47.mdl` and `ak47` all reach the same entry, which is the
## point: a caller holding a world model can ask what it fires without knowing that the answer
## lives on a different file.
static func lookup(model_or_name: String) -> Dictionary:
	_load()
	var key := model_or_name.get_file().get_basename().to_lower()
	for prefix in ["v_", "p_", "w_", "c_"]:
		if key.begins_with(prefix):
			key = key.substr(prefix.length())
			break
	return _weapons.get(key, {})


## The view model that belongs with this weapon, as a path relative to the game.
##
## The one worth having: a `w_` world model carries no sequences at all, so it has no firing
## animation and names no sound, and everything interesting about the weapon is on the `v_`.
static func view_model(model_or_name: String) -> String:
	return str(lookup(model_or_name).get("view_model", ""))


static func world_model(model_or_name: String) -> String:
	return str(lookup(model_or_name).get("world_model", ""))


## The sounds this weapon makes when it fires, in the order the game lists them.
##
## Several weapons have more than one take so that firing does not sound identical every time.
## Empty for a knife or a smoke grenade, which is the truth about them rather than a failure.
static func fire_sounds(model_or_name: String) -> PackedStringArray:
	var entry := lookup(model_or_name)
	var out := PackedStringArray()
	for s in entry.get("fire_sounds", []):
		out.append("sound/" + str(s))
	return out


## Every sound the weapon precaches: firing, reloading, the bolt, the pin.
static func all_sounds(model_or_name: String) -> PackedStringArray:
	var out := PackedStringArray()
	for s in lookup(model_or_name).get("sounds", []):
		out.append("sound/" + str(s))
	return out


## How fast a player carrying this can move, in metres per second, or 0.0 when unknown.
##
## Counter-Strike lets the weapon govern rather than the player: a knife runs at 250 units per
## second and an AWP at 210, dropping to 150 with the scope up. That is why a server sets
## sv_maxspeed absurdly high and lets this decide.
static func max_speed(model_or_name: String) -> float:
	var value = lookup(model_or_name).get("max_speed")
	return 0.0 if value == null else float(value) * 0.0254


## The animation suffix the game appends when playing a player's weapon stances, such as
## "ak47" in "run_ak47". It is how a character holding a rifle stands differently from one
## holding a pistol.
static func animation_extension(model_or_name: String) -> String:
	return str(lookup(model_or_name).get("animation_extension", ""))


static func _load() -> void:
	if _loaded:
		return
	_loaded = true

	var file := FileAccess.open(PATH, FileAccess.READ)
	if file == null:
		push_warning("Weapon manifest missing at %s; weapons fall back to guesswork." % PATH)
		return

	var parsed = JSON.parse_string(file.get_as_text())
	file.close()
	if not (parsed is Dictionary) or not (parsed as Dictionary).has("weapons"):
		push_warning("Weapon manifest at %s is not readable." % PATH)
		return

	_weapons = (parsed as Dictionary)["weapons"]
