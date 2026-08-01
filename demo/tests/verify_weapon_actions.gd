extends Node

## Can every weapon in Counter-Strike 1.6 be fired and reloaded?
##
## Three separate things were wrong and each hid the next. The sandbox asked for one sequence
## per role and got three of a rifle's six, so shoot2, shoot3 and reload were never imported.
## The reload key looked for a sequence called "reload", which the two pump shotguns do not
## have. And a model is allowed to give two sequences the same name, which Godot's
## AnimationLibrary refuses, so the second was dropped without a word.
##
## None of that is visible from inside: a weapon that fires with one animation instead of three
## looks like a weapon. So this counts.

const AnimationSets := preload("res://addons/quebratsk_editor/animation_sets.gd")
const REPORT := "user://weapon_actions.txt"

var _vfs: VFSManager
var _importer: UnifiedAssetImporter
var _report := PackedStringArray()


func _say(line: String) -> void:
	print(line)
	_report.append(line)


func _ready() -> void:
	_vfs = VFSManager.new()
	_importer = UnifiedAssetImporter.new()
	add_child(_vfs); add_child(_importer); _importer.set_vfs(_vfs)

	var root := "C:/Program Files (x86)/Steam/steamapps/common/Half-Life/cstrike"
	if not DirAccess.dir_exists_absolute(root):
		_say("cstrike is not installed at %s" % root)
		get_tree().quit()
		return
	var n := 0
	for a in _vfs.scan_game_directory(root).get("archives", []):
		if _vfs.mount_container("cs%d" % n, str(a)): n += 1
	_vfs.mount_directory("cs_loose", root)

	_say("=== EVERY COUNTER-STRIKE 1.6 WEAPON ===")
	_say("%-18s %5s %5s  %-26s %s" % ["model", "decl", "got", "attacks", "reload"])

	var hit: Dictionary = _vfs.find_files("models/v_", PackedStringArray(["mdl"]),
		PackedStringArray(["vvd", "vtx", "ani", "phy"]), PackedStringArray(), 0)

	var looked := 0
	var lost := 0
	var armed := 0
	var reloadable := 0
	# Weapons that genuinely have no reload: a grenade is thrown, a knife is swung, the C4 is
	# planted. Counting them as failures would make a correct result look broken.
	const NO_RELOAD := ["v_knife", "v_knife_r", "v_c4", "v_hegrenade", "v_flashbang",
		"v_smokegrenade"]

	for entry in hit["files"]:
		var uri := str(entry)
		var declared: PackedStringArray = _importer.list_poses(uri)
		if declared.is_empty():
			continue
		looked += 1

		var model: Node3D = _importer.load_model(uri, "", declared)
		if model == null:
			_say("%-18s could not be loaded" % uri.get_file())
			continue

		var anim: AnimationPlayer = null
		for node in _every_node(model):
			if node is AnimationPlayer:
				anim = node as AnimationPlayer
				break
		var got := PackedStringArray() if anim == null else PackedStringArray(anim.get_animation_list())

		# Every sequence the model declares has to come back. One that does not is one nobody
		# can ask for, and this is the only place that would notice.
		if got.size() < declared.size():
			lost += 1

		var attacks := AnimationSets.attacks(got)
		var reload := AnimationSets.reload_chain(got)
		if not attacks.is_empty():
			armed += 1
		if not reload.is_empty():
			reloadable += 1

		var bare := uri.get_file().get_basename()
		var reload_text := "-" if reload.is_empty() else reload[0]
		if reload.size() > 1:
			reload_text = "%s x%d then %s" % [reload[0], reload.size() - 2, reload[-1]]
		if reload.is_empty() and not NO_RELOAD.has(bare):
			reload_text = "MISSING"
		_say("%-18s %5d %5d  %-26s %s" % [uri.get_file(), declared.size(), got.size(),
			", ".join(attacks.slice(0, 3)) if not attacks.is_empty() else "NONE", reload_text])
		model.queue_free()

	_say("\n=== SUMMARY ===")
	_say("  %d weapons read" % looked)
	_say("  %d lost a sequence between being declared and being imported" % lost)
	_say("  %d have at least one attack animation" % armed)
	_say("  %d reload, of %d that should" % [reloadable, looked - NO_RELOAD.size()])

	var file := FileAccess.open(REPORT, FileAccess.WRITE)
	if file != null:
		file.store_string("\n".join(_report) + "\n")
		file.close()
		print("report written to %s" % ProjectSettings.globalize_path(REPORT))
	get_tree().quit()


func _every_node(root: Node) -> Array[Node]:
	var out: Array[Node] = [root]
	for child in root.get_children():
		out.append_array(_every_node(child))
	return out
