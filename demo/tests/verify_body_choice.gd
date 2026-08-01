extends Node

## Does asking for a different version of a part actually build a different model?
##
## Reporting that `v_357.mdl` has a scope that is either blank or a laser sight is worth
## nothing if asking for the laser sight hands back the blank one. The only proof is the
## geometry: load the model once with the default and once per alternative, and compare what
## came out. Identical vertex counts across every choice means the choice was ignored.
##
## Counting vertices rather than eyeballing it is deliberate. A scope is a handful of
## triangles on a model with thousands, and it is entirely possible to look at both and see
## no difference.

## Where the report is written as well as printed. Mounting every installed game takes long
## enough that re-running just to re-read the tail is wasteful.
const REPORT := "user://body_choice_report.txt"

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
	var games: Dictionary = SteamLibraryDetector.detect_installed_games()
	var n := 0
	for t in games:
		var root := str(games[t])
		for a in _vfs.scan_game_directory(root).get("archives", []):
			if _vfs.mount_container("g%d" % n, str(a)): n += 1
		_vfs.mount_directory("l%d" % n, root); n += 1

	_say("\n=== CHOOSING A VERSION OF A PART ===")

	var files := PackedStringArray()
	for needle in ["models/v_", "models/p_", "models/player/"]:
		var found: Dictionary = _vfs.find_files(needle, PackedStringArray(["mdl"]),
			PackedStringArray(["vvd", "vtx", "ani", "phy"]), PackedStringArray(), 60)
		for f in found["files"]:
			files.append(str(f))

	var tried := 0
	var changed := 0
	var ignored := 0

	for entry in files:
		if tried >= 8: break
		var uri := str(entry)
		var head := _vfs.read_file(uri)
		if head.size() < 8 or head.decode_s32(4) != 10:
			continue  # GoldSrc only; Source goes through a parser that reads none of this

		var groups: Array = _importer.list_body_groups(uri)
		if groups.is_empty():
			continue
		tried += 1

		var part := str((groups[0] as Dictionary)["name"])
		var options: PackedStringArray = (groups[0] as Dictionary)["options"]
		var baseline := _vertex_count(uri, {})
		_say("\n%s   part '%s', %d versions, default builds %d vertices"
			% [uri.get_file(), part, options.size(), baseline])

		for i in range(options.size()):
			var count := _vertex_count(uri, {part: i})
			var verdict := "same as default"
			if count != baseline:
				verdict = "%+d vertices" % (count - baseline)
			_say("   #%d %-28s %6d  (%s)" % [i, options[i], count, verdict])

		# Version 0 is the default, so a working choice differs on at least one of the rest.
		var moved := false
		for i in range(1, options.size()):
			if _vertex_count(uri, {part: i}) != baseline:
				moved = true
		if moved:
			changed += 1
		else:
			ignored += 1
			_say("   ^ every version built the same geometry, so the choice did nothing")

	_say("\n=== SUMMARY ===")
	_say("  %d models with a part that has alternatives" % tried)
	_say("  %d built different geometry when asked for a different version" % changed)
	_say("  %d ignored the request" % ignored)

	var file := FileAccess.open(REPORT, FileAccess.WRITE)
	if file != null:
		file.store_string("\n".join(_report) + "\n")
		file.close()
		print("report written to %s" % ProjectSettings.globalize_path(REPORT))
	get_tree().quit()


## How many vertices a model builds under a given set of choices.
##
## Walks to the MeshInstance3D because load_model() hands back a Skeleton3D for anything with
## bones and the mesh hangs beneath it.
func _vertex_count(uri: String, choices: Dictionary) -> int:
	var node := _importer.load_model(uri, "", PackedStringArray(), choices)
	if node == null:
		return -1
	var total := 0
	for child in _every_node(node):
		var mesh_instance := child as MeshInstance3D
		if mesh_instance == null or mesh_instance.mesh == null:
			continue
		var mesh: Mesh = mesh_instance.mesh
		for s in range(mesh.get_surface_count()):
			total += (mesh.surface_get_arrays(s)[Mesh.ARRAY_VERTEX] as PackedVector3Array).size()
	node.queue_free()
	return total


func _every_node(root: Node) -> Array[Node]:
	var out: Array[Node] = [root]
	for child in root.get_children():
		out.append_array(_every_node(child))
	return out
