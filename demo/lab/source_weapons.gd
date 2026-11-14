extends RefCounted

## What a Source weapon does, written down per weapon.
##
## A GoldSrc view model carries its own sounds and its muzzle inside the .mdl. A Source one
## made for Garry's Mod does not: the model holds the shapes and the movements, and a script
## beside it says which movement is the reload, what is heard and when, and how many rounds
## it holds. That script is not run here. What it states is copied out as plain values, and
## only for the packs somebody has on their own machine: none of the files are part of this
## project.
##
## Times are seconds from the start of the movement; sounds are under sound/.

const AK := {
	"name": "uplp_ak",
	"model": "models/weapons/arc9/c_uplp_ak.mdl",
	"hands": "models/player/darsu/c_hozq_v4_rig.mdl",
	"idle": "idle",
	"draw": "draw",
	"attacks": ["fire_01", "fire_02", "fire_03"],
	"reload": "reload_762",
	"reload_empty": "reload_empty_762",
	"clip": 30,
	"rounds_per_minute": 600,
	"fov": 70.0,
	# Stand-ins the model carries for a magazine being swapped, shown only by the script.
	"hidden_bones": ["fakemag", "fakerounds"],
	"fire": ["uplp_urban_temp/ak/762/fire-01.wav", "uplp_urban_temp/ak/762/fire-02.wav",
		"uplp_urban_temp/ak/762/fire-03.wav"],
	"mechanism": ["uplp_urban_temp/ak/mech-01.wav", "uplp_urban_temp/ak/mech-02.wav",
		"uplp_urban_temp/ak/mech-03.wav", "uplp_urban_temp/ak/mech-04.wav"],
	"dry": "uplp_urban_temp/ak/dryfire.wav",
	"events": {
		"draw": [["uplp_urban_temp/common/cloth_3.ogg", 0.0],
			["uplp_urban_temp/common/raise.ogg", 2.0 / 30.0]],
		"reload_762": [["uplp_urban_temp/common/magpouch.ogg", 0.0],
			["uplp_urban_temp/ak/reload_start.ogg", 0.025],
			["uplp_urban_temp/ak/magrelease.ogg", 9.0 / 30.0],
			["uplp_urban_temp/ak/bonk.ogg", 11.0 / 30.0],
			["uplp_rz/ak/magout.ogg", 12.0 / 30.0],
			["uplp_urban_temp/ak/magin.ogg", 22.0 / 30.0],
			["uplp_urban_temp/common/rattle1.ogg", 36.0 / 30.0]],
		"reload_empty_762": [["uplp_urban_temp/common/magpouch.ogg", 0.0],
			["uplp_urban_temp/ak/reload_start.ogg", 0.025],
			["uplp_urban_temp/ak/magrelease.ogg", 9.0 / 30.0],
			["uplp_urban_temp/ak/bonk.ogg", 11.0 / 30.0],
			["uplp_rz/ak/magout.ogg", 12.0 / 30.0],
			["uplp_urban_temp/ak/magin.ogg", 22.0 / 30.0],
			["uplp_rz/ak/chback.ogg", 41.0 / 30.0],
			["uplp_rz/ak/chforward.ogg", 48.0 / 30.0],
			["uplp_urban_temp/common/rattle2.ogg", 56.0 / 30.0]],
	},
}

const ALL := [AK]


## The file a path names in whatever is mounted, or nothing.
static func find(vfs, path: String) -> String:
	var hit: Dictionary = vfs.find_files(path.get_file().get_basename(),
		PackedStringArray([path.get_extension()]), PackedStringArray(), PackedStringArray(), 64)
	for file in hit.get("files", []):
		if str(file).to_lower().ends_with(path.to_lower()):
			return str(file)
	return ""


## The first weapon written down here whose model is mounted.
static func pick(vfs) -> Dictionary:
	for card in ALL:
		if not find(vfs, str(card["model"])).is_empty():
			return card
	return {}
