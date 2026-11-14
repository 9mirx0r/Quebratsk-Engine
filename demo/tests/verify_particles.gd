extends Node3D

## Does a Source particle file come out as an effect that can be fired?
##
## Reads every particle file in what lab/source.local.txt mounts, then builds the stand-in
## muzzle flash, checks it has something to draw with a picture on it, fires it in front of
## a camera and keeps a picture of it. The lab cannot show it without the game installed,
## which this does not need. Writes the picture to the path given after `--`, if any.

const SourceParticles := preload("res://lab/source_particles.gd")
const SourceWeapons := preload("res://lab/source_weapons.gd")
const NOTE := "res://lab/source.local.txt"

var vfs: VFSManager
var importer: UnifiedAssetImporter
var _failures := 0


func _check(what: String, ok: bool) -> void:
	if not ok:
		_failures += 1
		print("  FAIL ", what)


func _ready() -> void:
	vfs = VFSManager.new()
	importer = UnifiedAssetImporter.new()
	add_child(vfs)
	add_child(importer)
	importer.set_vfs(vfs)
	var note := FileAccess.open(NOTE, FileAccess.READ)
	var n := 0
	while note != null and not note.eof_reached():
		var line := note.get_line().strip_edges()
		if line.to_lower().ends_with("_dir.vpk"):
			vfs.mount_container("a%d" % n, line)
		elif not line.is_empty() and DirAccess.dir_exists_absolute(line):
			vfs.mount_directory("d%d" % n, line)
		n += 1

	print("\n=== PARTICLE FILES ===")
	var hit: Dictionary = vfs.find_files("particles/", PackedStringArray(["pcf"]),
		PackedStringArray(), PackedStringArray(), 400)
	var systems := 0
	for uri in hit["files"]:
		var read: Array = importer.load_particles(str(uri))
		_check("%s read" % uri, not read.is_empty())
		systems += read.size()
	print("  %d file(s), %d system(s)" % [hit["files"].size(), systems])
	_check("some particle file mounted", hit["files"].size() > 0)

	var system := SourceWeapons.STAND_IN_FLASH
	var file := SourceParticles.find_file(self, system)
	_check("'%s' found" % system, not file.is_empty())
	if file.is_empty():
		_finish()
		return
	var effect := SourceParticles.build(self, file, system)
	_check("built", effect != null)
	if effect == null:
		_finish()
		return
	var parts: Array = effect.find_children("*", "GPUParticles3D", true, false)
	print("  '%s' from %s: %d part(s)" % [system, file.get_file(), parts.size()])
	_check("something to draw", parts.size() > 0)
	for p in parts:
		var part := p as GPUParticles3D
		var quad := part.draw_pass_1 as QuadMesh
		var surface := quad.material as StandardMaterial3D if quad != null else null
		var picture := surface.albedo_texture if surface != null else null
		print("    %-28s %3d particle(s), lives %.3f s, %.1f cm wide, picture %s"
			% [part.name, part.amount, part.lifetime, quad.size.x * 100.0 if quad else 0.0,
			   "yes" if picture != null else "NO"])
		_check("%s has a picture" % part.name, picture != null)

	# Is the weapon's own muzzle point there for it to hang on?
	var gun := SourceWeapons.find(vfs, str(SourceWeapons.AK["model"]))
	if not gun.is_empty():
		var named := false
		var names := []
		for a in importer.list_attachments(gun):
			names.append(str(a["name"]))
			named = named or str(a["name"]).to_lower() == str(SourceWeapons.AK["muzzle"])
		print("  %s points: %s" % [gun.get_file(), ", ".join(names)])
		print("  %s: muzzle point %s" % [gun.get_file(), "yes" if named else "MISSING"])
		_check("the weapon has its muzzle point", named)

	# Fired in front of a camera, side on, and kept as a picture just after.
	var camera := Camera3D.new()
	add_child(camera)
	camera.position = Vector3(0.45, 0.0, -0.25)
	camera.look_at(Vector3(0.0, 0.0, -0.25), Vector3.UP)
	camera.fov = 50.0
	var world := WorldEnvironment.new()
	world.environment = Environment.new()
	world.environment.background_mode = Environment.BG_COLOR
	world.environment.background_color = Color(0.06, 0.06, 0.07)
	add_child(world)
	add_child(effect)
	await get_tree().process_frame
	effect.fire()
	await get_tree().create_timer(0.035).timeout
	await RenderingServer.frame_post_draw
	var args := OS.get_cmdline_user_args()
	if not args.is_empty():
		get_viewport().get_texture().get_image().save_png(args[0])
		print("  picture: ", args[0])
	_finish()


func _finish() -> void:
	print("verify_particles: %s" % ("all passed" if _failures == 0 else "%d failed" % _failures))
	get_tree().quit(1 if _failures > 0 else 0)
