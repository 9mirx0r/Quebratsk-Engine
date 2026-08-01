extends Node3D

## What a shot leaves behind: a hole in the wall, or blood.
##
## Both are decals, and a GoldSrc decal is an ordinary texture in a .wad whose name begins with
## a brace — `{shot1`, `{blood3` — the brace meaning the palette's last colour is transparent.
## The engine keeps a table of them and picks one at random per hit, out of `gDecals` in
## world.cpp and `UTIL_BloodDecalTrace`.
##
##     {shot1 … {shot5     bullet holes
##     {blood1 … {blood6   blood, red
##     {yblood1 … {yblood6 blood, the other kind
##
## Blood itself is two sprites thrown from the wound, `sprites/bloodspray.spr` and
## `sprites/blood.spr`, with the count taken from how hard the hit was: amount / 10, held
## between 3 and 16, and doubled in a multiplayer game. `UTIL_RandomBloodVector` throws them
## outward and upward — x and y between -1 and 1, z between 0 and 1 — because blood does not
## fall straight down out of a standing body.
##
## Decals only stick to level geometry. `UTIL_DecalTrace` gives up when what it hit is not a
## BSP model, which is why shooting a person never marks them.

const BULLET_HOLES := ["{shot1", "{shot2", "{shot3", "{shot4", "{shot5"]

## Six, because that is what UTIL_BloodDecalTrace picks between: DECAL_BLOOD1 plus a random
## number up to five. decals.wad actually holds eight, along with six handprints, and the
## engine never reaches the last two from a gunshot.
const BLOOD_MARKS := ["{blood1", "{blood2", "{blood3", "{blood4", "{blood5", "{blood6"]

const HOLE_SIZE := 0.09
const BLOOD_SIZE := 0.42

## How many marks a level keeps before the oldest goes. GoldSrc has r_decals, 300 by default,
## and for the same reason: a long game otherwise ends up with a decal on every surface.
const MAX_MARKS := 220

var _lab: Node3D
var _marks: Array[Node3D] = []
var _textures := {}
var _blood_sprite: Texture2D
var _rng := RandomNumberGenerator.new()


func setup(lab: Node3D) -> void:
	_lab = lab
	_rng.randomize()

	# Read now, at the cost of a moment while the level loads, rather than on the first shot.
	# Finding a texture means searching every mounted container, and doing that when somebody
	# pulls a trigger stalls the frame — the same mistake the debris models made, so the same
	# answer.
	var found := 0
	for named in BULLET_HOLES + BLOOD_MARKS:
		if _texture_named(str(named)) != null:
			found += 1
	_blood_sprite = _texture_named("sprites/bloodspray.spr")
	print("[marks] %d of %d decal(s) found in the game's wads"
		% [found, BULLET_HOLES.size() + BLOOD_MARKS.size()])


## A bullet hit something that is not alive.
func bullet_hole(at: Vector3, normal: Vector3) -> void:
	_place(BULLET_HOLES[_rng.randi_range(0, BULLET_HOLES.size() - 1)], at, normal, HOLE_SIZE)


## A bullet hit somebody. The spray, and the mark it leaves on whatever is behind them.
func blood(at: Vector3, direction: Vector3, amount: int) -> void:
	_spray(at, direction, amount)

	# The mark goes on the wall behind, found by carrying on through the body. A shot that
	# passes into open air leaves nothing, which is right.
	var space := get_world_3d().direct_space_state
	var query := PhysicsRayQueryParameters3D.create(at, at + direction.normalized() * 6.0)
	var hit := space.intersect_ray(query)
	if hit.is_empty():
		return
	_place(BLOOD_MARKS[_rng.randi_range(0, BLOOD_MARKS.size() - 1)],
		hit["position"] as Vector3, hit["normal"] as Vector3, BLOOD_SIZE)


## The spray itself.
func _spray(at: Vector3, direction: Vector3, amount: int) -> void:
	# Doubled the way a multiplayer game doubles it, then held between 3 and 16.
	var count := clampi(int(amount * 2 / 10.0), 3, 16)

	var mesh := QuadMesh.new()
	mesh.size = Vector2(0.12, 0.12)
	var material := StandardMaterial3D.new()
	material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	material.billboard_mode = BaseMaterial3D.BILLBOARD_ENABLED
	material.albedo_color = Color(0.55, 0.03, 0.03)
	if _blood_sprite != null:
		material.albedo_texture = _blood_sprite
	mesh.material = material

	var process := ParticleProcessMaterial.new()
	process.emission_shape = ParticleProcessMaterial.EMISSION_SHAPE_SPHERE
	process.emission_sphere_radius = 0.08
	# Outward and upward, which is what UTIL_RandomBloodVector describes.
	process.direction = (direction.normalized() + Vector3.UP).normalized()
	process.spread = 55.0
	process.initial_velocity_min = 1.5
	process.initial_velocity_max = 4.0
	process.gravity = Vector3(0, -9.8, 0)
	process.scale_min = 0.5
	process.scale_max = 1.3

	var burst := GPUParticles3D.new()
	burst.draw_pass_1 = mesh
	burst.process_material = process
	burst.amount = count
	burst.lifetime = 0.9
	burst.one_shot = true
	burst.explosiveness = 1.0
	add_child(burst)
	burst.global_position = at
	burst.emitting = true
	get_tree().create_timer(1.4).timeout.connect(burst.queue_free)


## Put a mark on a surface, lying flat against it.
func _place(named: String, at: Vector3, normal: Vector3, size: float) -> void:
	var texture := _texture_named(named)
	if texture == null:
		return

	var mesh := QuadMesh.new()
	mesh.size = Vector2(size, size)
	var material := StandardMaterial3D.new()
	material.albedo_texture = texture
	material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA_SCISSOR
	material.shading_mode = BaseMaterial3D.SHADING_MODE_PER_PIXEL
	# Off the wall by a millimetre, or it flickers against the surface it is drawn on.
	material.cull_mode = BaseMaterial3D.CULL_DISABLED
	mesh.material = material

	var mark := MeshInstance3D.new()
	mark.mesh = mesh
	add_child(mark)
	mark.global_position = at + normal * 0.005
	# A quad faces +Z, so it is turned to look along the surface's own normal. Straight up and
	# straight down need a different reference or the basis collapses.
	var up := Vector3.UP if absf(normal.dot(Vector3.UP)) < 0.99 else Vector3.RIGHT
	mark.look_at(mark.global_position + normal, up)
	mark.rotate_object_local(Vector3.FORWARD, _rng.randf_range(0.0, TAU))

	_marks.append(mark)
	while _marks.size() > MAX_MARKS:
		var oldest: Node3D = _marks.pop_front()
		if is_instance_valid(oldest):
			oldest.queue_free()


## A decal texture out of the game's .wad files, read once and kept.
func _texture_named(named: String) -> Texture2D:
	if _textures.has(named):
		return _textures[named]
	var texture: Texture2D = null
	if _lab != null and _lab.importer != null:
		texture = _lab.importer.load_texture(named)
	_textures[named] = texture
	if texture == null:
		push_warning("[marks] no decal called '%s'" % named)
	return texture
