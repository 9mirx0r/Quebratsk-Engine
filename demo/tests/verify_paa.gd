extends Node

## Does the PAA reader work on files nobody in this project wrote?
##
## Its unit test builds PAAs from the same reading of the format the parser was written
## from, so the two agreeing proves only that they agree. This runs it against a retail
## DayZ install, which is the check that decides whether .paa can be called supported.

const DAYZ := "C:/Program Files (x86)/Steam/steamapps/common/DayZ"
const WORKSHOP := "C:/Program Files (x86)/Steam/steamapps/workshop/content/221100"

## Enough to be representative without turning a verification run into a coffee break.
const SAMPLE := 1500


func _ready() -> void:
	var vfs := VFSManager.new()
	var importer := UnifiedAssetImporter.new()
	add_child(vfs)
	add_child(importer)
	importer.set_vfs(vfs)

	print("\n=== PAA VERIFICATION ===")
	var mounted := _mount(vfs, DAYZ, "dz")
	print("mounted %d archives from the base game" % mounted)
	mounted += _mount_mods(vfs)

	var hit: Dictionary = vfs.find_files("", PackedStringArray(["paa"]), PackedStringArray(), PackedStringArray(), 0)
	var all: PackedStringArray = hit["files"]
	print("%d .paa files reachable" % all.size())
	if all.is_empty():
		print("nothing to check")
		get_tree().quit()
		return

	_survey(vfs, all)
	_decode(vfs, importer, all)

	print("\n=== DONE ===")
	get_tree().quit()


func _mount(vfs: VFSManager, root: String, prefix: String) -> int:
	var scan: Dictionary = vfs.scan_game_directory(root)
	var n := 0
	for archive in scan.get("archives", []):
		if vfs.mount_container("%s%d" % [prefix, n], str(archive)):
			n += 1
	return n


## Workshop mods are a second population entirely: made by different people with different
## versions of the tools, which is where a reader that only ever saw first-party output
## tends to fall over.
func _mount_mods(vfs: VFSManager) -> int:
	if not DirAccess.dir_exists_absolute(WORKSHOP):
		print("no workshop mods installed")
		return 0
	var n := 0
	var mods := 0
	for entry in DirAccess.get_directories_at(WORKSHOP):
		if mods >= 8:
			break
		n += _mount(vfs, WORKSHOP.path_join(str(entry)), "mod%d_" % mods)
		mods += 1
	print("mounted %d archives from %d workshop mods" % [n, mods])
	return n


## Read the leading word directly, so the population is known independently of whether the
## parser agrees about it.
func _survey(vfs: VFSManager, all: PackedStringArray) -> void:
	const TYPES := {
		0xFF01: "DXT1", 0xFF02: "DXT2", 0xFF03: "DXT3", 0xFF04: "DXT4", 0xFF05: "DXT5",
		0x4444: "ARGB4444", 0x1555: "ARGB1555", 0x8888: "ARGB8888", 0x8080: "AI88",
	}
	var kinds := {}
	var checked := 0
	for entry in all:
		if checked >= SAMPLE:
			break
		var bytes := vfs.read_file(str(entry))
		if bytes.size() < 4:
			continue
		var t := bytes.decode_u16(0)
		var label: String = TYPES.get(t, "unknown 0x%04x" % t)
		kinds[label] = int(kinds.get(label, 0)) + 1
		checked += 1

	print("\npixel layouts across %d files:" % checked)
	for k in kinds:
		print("   %-16s %5d" % [k, kinds[k]])


## The real test: hand each file to the loader and see what comes back. A texture with
## plausible dimensions is the only evidence that counts.
func _decode(vfs: VFSManager, importer: UnifiedAssetImporter, all: PackedStringArray) -> void:
	var ok := 0
	var failed := 0
	var widths := {}
	var first_ok := ""
	var t0 := Time.get_ticks_msec()

	var checked := 0
	for entry in all:
		if checked >= SAMPLE:
			break
		checked += 1
		var uri := str(entry)
		var tex: Texture2D = importer.load_texture(uri)
		if tex == null:
			failed += 1
			continue
		ok += 1
		var key := "%dx%d" % [tex.get_width(), tex.get_height()]
		widths[key] = int(widths.get(key, 0)) + 1
		if first_ok.is_empty():
			first_ok = "%s  %s" % [uri.get_file(), key]

	var ms := Time.get_ticks_msec() - t0
	print("\ndecoded %d of %d (%.1f%%) in %d ms" % [ok, checked, 100.0 * ok / maxi(checked, 1), ms])
	if not first_ok.is_empty():
		print("   first success: %s" % first_ok)

	# Dimensions are the cheapest sanity check there is: a texture that decoded but landed
	# on a size no artist would author says the header was read at the wrong offset.
	var sizes := widths.keys()
	sizes.sort()
	var shown := 0
	for s in sizes:
		if shown >= 10:
			break
		print("   %-12s %5d" % [s, widths[s]])
		shown += 1
	if sizes.size() > 10:
		print("   ... and %d more sizes" % (sizes.size() - 10))
