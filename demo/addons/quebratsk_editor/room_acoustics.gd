@tool
extends RefCounted

## What a room sounds like, from the room type a map declares.
##
## A GoldSrc level places `env_sound` entities: a point, a radius, and a `roomtype` from 0 to
## 28. Standing inside one changes how everything sounds — a gunshot in a tunnel is not a
## gunshot in the open. `cs_siege` places forty of them, `de_dust` two, and Quebratsk read
## none: every level sounded like a field.
##
## Where the numbers come from, and where they do not.
##
## The room *names* are the game's own, out of the FGD the level editors used: "Metal Small",
## "Tunnel Large", "Cavern Medium", and they are systematic — a material and a size. The
## actual DSP coefficients behind them live in the GoldSrc engine, which is closed, so they
## are not available to read and are not reproduced here.
##
## So this is an interpretation and says so. Size sets how long the reverberation runs and how
## late the first reflection arrives; material sets how much is absorbed and how bright what
## comes back is. A tunnel is long and narrow; a cavern is enormous and soft; metal gives back
## almost everything; concrete swallows the top end. Anyone who disagrees with a number can
## change it here and hear the difference, which is the point of it being a table.

## Material, by the word the room type begins with. Damping is how much of the high end is
## lost on each bounce, wet is how much of the room you hear over the sound itself.
const MATERIALS := {
	"metal":    {"damping": 0.10, "wet": 0.45, "hipass": 0.02},
	"tunnel":   {"damping": 0.25, "wet": 0.55, "hipass": 0.10},
	"chamber":  {"damping": 0.35, "wet": 0.42, "hipass": 0.05},
	"bright":   {"damping": 0.08, "wet": 0.40, "hipass": 0.00},
	"water":    {"damping": 0.70, "wet": 0.60, "hipass": 0.30},
	"concrete": {"damping": 0.50, "wet": 0.38, "hipass": 0.05},
	"big":      {"damping": 0.40, "wet": 0.50, "hipass": 0.03},
	"cavern":   {"damping": 0.60, "wet": 0.62, "hipass": 0.08},
	"weirdo":   {"damping": 0.30, "wet": 0.70, "hipass": 0.15},
	"generic":  {"damping": 0.40, "wet": 0.30, "hipass": 0.05},
}

## Size, by the word the room type ends with. The numbered variants — "Water 1", "Big 3" —
## count as small, medium and large in order, which is what those numbers mean.
const SIZES := {
	"small":  {"room_size": 0.35, "predelay": 12.0},
	"medium": {"room_size": 0.65, "predelay": 28.0},
	"large":  {"room_size": 0.92, "predelay": 55.0},
}


## The reverberation for a room type, or null for 0, which is the game's own "off".
static func reverb_for(roomtype: int, name := "") -> AudioEffectReverb:
	if roomtype <= 0:
		return null

	var lowered := name.to_lower()
	var material := {}
	for key in MATERIALS:
		if lowered.begins_with(str(key)):
			material = MATERIALS[key]
			break
	if material.is_empty():
		material = MATERIALS["generic"]

	var size := SIZES["medium"]
	if lowered.ends_with("small") or lowered.ends_with(" 1"):
		size = SIZES["small"]
	elif lowered.ends_with("large") or lowered.ends_with(" 3"):
		size = SIZES["large"]

	var reverb := AudioEffectReverb.new()
	reverb.room_size = float(size["room_size"])
	reverb.predelay_msec = float(size["predelay"])
	reverb.damping = float(material["damping"])
	reverb.hipass = float(material["hipass"])
	reverb.wet = float(material["wet"])
	reverb.dry = 1.0
	reverb.spread = 0.9
	return reverb


## The name a roomtype number goes by, from the entity catalogue rather than from memory.
static func name_of(roomtype: int, catalogue) -> String:
	var described: Dictionary = catalogue.describe("env_sound")
	var keys: Dictionary = described.get("keys", {})
	var choices: Dictionary = (keys.get("roomtype", {}) as Dictionary).get("choices", {})
	return str(choices.get(str(roomtype), ""))
