extends Node

## Verification harness for the six APIs added in a0d7402.
## Each check states what §6 of docs/API.md asked for, then what actually happens.

const GMOD := "C:/Program Files (x86)/Steam/steamapps/common/GarrysMod/garrysmod"
const WORKSHOP := "C:/Program Files (x86)/Steam/steamapps/workshop/content/4000/3582369292"
const MODEL := "models/player/lowpoly/usarmy_2000.mdl"

var vfs: VFSManager
var importer: UnifiedAssetImporter
var async_importer: AsyncAssetImporter
var model_uri := ""
## Set once the async check has an answer, so the deadline below cannot report a timeout
## for a callback that already arrived.
var _answered := false


func _ready() -> void:
	vfs = VFSManager.new()
	importer = UnifiedAssetImporter.new()
	async_importer = AsyncAssetImporter.new()
	add_child(vfs)
	add_child(importer)
	add_child(async_importer)
	importer.set_vfs(vfs)

	_mount()
	_check_steam()
	_check_mounts_info()
	_check_scan()
	_check_list_poses()
	_check_error_codes()
	_check_load_model_async()


func _mount() -> void:
	var n := 0
	var dir := DirAccess.open(GMOD)
	if dir != null:
		for f in dir.get_files():
			if f.ends_with("_dir.vpk"):
				if vfs.mount_container("gmod%d" % n, GMOD + "/" + f):
					n += 1
	var wdir := DirAccess.open(WORKSHOP)
	if wdir != null:
		for f in wdir.get_files():
			if f.get_extension().to_lower() == "gma":
				if vfs.mount_container("addon", WORKSHOP + "/" + f):
					model_uri = "vfs://addon/" + MODEL
				break
	print("\n[SETUP] %d gmod VPKs + addon; model_uri=%s exists=%s"
		% [n, model_uri, vfs.file_exists(model_uri)])


func _check_steam() -> void:
	print("\n=== 1. SteamLibraryDetector (asked: add Half-Life 2 + Source titles) ===")
	var games: Dictionary = SteamLibraryDetector.detect_installed_games()
	for key in games:
		print("  %-28s %s" % [key, games[key]])
	print("  -> Half-Life 2 detected: %s" % games.has("Half-Life 2"))


func _check_mounts_info() -> void:
	print("\n=== 2. VFSManager.get_mounts_info() (asked: prefix/real_path/engine/file_count) ===")
	var t0 := Time.get_ticks_msec()
	var mounts: Array = vfs.get_mounts_info()
	var dt := Time.get_ticks_msec() - t0
	print("  %d mounts in %d ms" % [mounts.size(), dt])
	var total := 0
	for m in mounts:
		total += int(m.get("file_count", 0))
	for m in mounts:
		print("    %-8s engine=%-8s files=%-6d archives=%d"
			% [m.get("prefix"), m.get("engine"), m.get("file_count", -1), m.get("archive_count", -1)])
	print("  -> summed file_count=%d ; vfs.list_files().size()=%d" % [total, vfs.list_files().size()])


func _check_scan() -> void:
	print("\n=== 3. VFSManager.scan_game_directory() (asked: 'what can I import here?') ===")
	var t0 := Time.get_ticks_msec()
	var r: Dictionary = vfs.scan_game_directory(GMOD)
	var dt := Time.get_ticks_msec() - t0
	print("  took %d ms (main thread, blocking)" % dt)
	for key in ["error", "total_archives", "loose_models", "loose_maps", "loose_textures"]:
		if r.has(key):
			print("    %-16s %s" % [key, r[key]])


func _check_list_poses() -> void:
	print("\n=== 4. UnifiedAssetImporter.list_poses() (asked: CHEAP header-only scan) ===")
	if model_uri == "":
		print("  SKIP: model not mounted")
		return
	var t0 := Time.get_ticks_msec()
	var poses: PackedStringArray = importer.list_poses(model_uri)
	var dt_list := Time.get_ticks_msec() - t0

	t0 = Time.get_ticks_msec()
	var node := importer.load_model(model_uri)
	var dt_full := Time.get_ticks_msec() - t0
	var full_poses := 0
	if node != null:
		full_poses = node.get_meta("quebratsk_poses", PackedStringArray()).size()
		node.queue_free()

	print("  list_poses  -> %d poses in %d ms" % [poses.size(), dt_list])
	print("  load_model  -> %d poses in %d ms (full import incl. Godot nodes)" % [full_poses, dt_full])
	print("  -> cost ratio list_poses/load_model = %.2f  (a header-only scan should be ~0.0x)"
		% (float(dt_list) / maxf(1.0, float(dt_full))))


func _check_error_codes() -> void:
	print("\n=== 5. get_last_error_code() (asked: UI-catchable failures) ===")
	# Force a clean OK first.
	if model_uri != "":
		var ok_node := importer.load_model(model_uri)
		if ok_node != null:
			ok_node.queue_free()
	print("  after a SUCCESSFUL load_model : %d" % importer.get_last_error_code())

	var bogus := importer.load_model("vfs://addon/models/this_does_not_exist.mdl")
	print("  after a FAILED load_model     : %d  (node=%s)"
		% [importer.get_last_error_code(), "null" if bogus == null else "non-null"])

	var bogus_mesh := importer.load_mesh("vfs://addon/models/this_does_not_exist.mdl")
	print("  after a FAILED load_mesh      : %d  (mesh=%s)"
		% [importer.get_last_error_code(), "null" if bogus_mesh == null else "non-null"])
	print("  named constants visible in GDScript: OK=%d VFS=%d UNREADABLE=%d PARSE=%d"
		% [UnifiedAssetImporter.ERR_OK, UnifiedAssetImporter.ERR_VFS_NOT_SET,
		   UnifiedAssetImporter.ERR_ASSET_UNREADABLE, UnifiedAssetImporter.ERR_PARSE_FAILED])


func _check_load_model_async() -> void:
	print("\n=== 6. load_model_async() (asked: no main-thread stall) ===")
	if model_uri == "":
		print("  SKIP: model not mounted")
		return
	async_importer.load_model_async(importer, model_uri, "idle_smg1",
		Callable(self, "_on_model_ready"))
	print("  request dispatched, waiting for callback...")

	# A callback that never arrives is the failure this check exists to catch, so it has to
	# end the run rather than leave the harness sitting there looking busy.
	var deadline := get_tree().create_timer(30.0)
	deadline.timeout.connect(func() -> void:
		if not _answered:
			print("  NO CALLBACK within 30 s  ** FAILED **")
			_finish())


func _finish() -> void:
	_answered = true
	print("\n=== VERIFICATION COMPLETE ===")
	get_tree().quit()


func _on_model_ready(node) -> void:
	if node == null:
		print("  CALLBACK -> null  ** FAILED **")
	else:
		var bones: int = 0
		if node is Skeleton3D:
			bones = node.get_bone_count()
		print("  CALLBACK -> %s  bones=%d  poses=%d"
			% [node.get_class(), bones, node.get_meta("quebratsk_poses", PackedStringArray()).size()])
		node.queue_free()
	_finish()
