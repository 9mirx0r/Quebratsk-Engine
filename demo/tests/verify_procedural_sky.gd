extends Node3D

## Does the computed sky put the sun where a level says it is, and does it take the place of
## the level's own painted sky?
##
## Needs no game installed: the "level" here is a bare node with a "Sky" child, and its
## entities are written out by hand. What the sky looks like is for tools/visual_check.py
## and a pair of eyes; this judges the parts that have a right answer.

const DistantBackdrop := preload("res://addons/quebratsk_editor/distant_backdrop.gd")
const ProceduralSky := preload("res://addons/quebratsk_editor/procedural_sky.gd")

var _failures := 0


func _check(what: String, ok: bool) -> void:
	if ok:
		print("  ok      %s" % what)
	else:
		_failures += 1
		print("  WRONG   %s" % what)


func _near(a: Vector3, b: Vector3) -> bool:
	return a.distance_to(b) < 0.01


func _sun(keys: Dictionary) -> Vector3:
	keys["classname"] = "light_environment"
	return ProceduralSky.sun_of([{"classname": "worldspawn"}, keys])["direction"]


func _ready() -> void:
	print("=== verify_procedural_sky ===")

	# The level's (x, y, z) is Godot's (-y, z, -x). Light going straight down means a sun
	# straight overhead.
	_check("pitch -90 is a sun overhead", _near(_sun({"pitch": "-90"}), Vector3.UP))
	# Light travelling along the level's +x and down 45 degrees came from -x and above,
	# which is Godot's +z and up.
	_check("yaw 0, pitch -45 comes from -x",
		_near(_sun({"angles": "0 0 0", "pitch": "-45"}), Vector3(0, 1, 1).normalized()))
	# Along the level's +y: the sun is at -y, Godot's +x.
	_check("yaw 90, pitch -45 comes from -y",
		_near(_sun({"angles": "0 90 0", "pitch": "-45"}), Vector3(1, 1, 0).normalized()))
	_check("`pitch` wins over the pitch in `angles`",
		_near(_sun({"angles": "-10 0 0", "pitch": "-90"}), Vector3.UP))
	_check("`angles` alone is enough",
		_near(_sun({"angles": "-90 0 0"}), Vector3.UP))
	# Light pointing upwards would put the sun under the ground; it is lifted to just
	# above the horizon instead.
	var low := _sun({"angles": "0 0 0", "pitch": "30"})
	_check("a sun below the horizon is lifted above it", low.y > 0.0 and low.y < 0.2)

	var none: Dictionary = ProceduralSky.sun_of([{"classname": "worldspawn"}])
	_check("no light_environment gives the default sun",
		not none["found"] and none["direction"].y > 0.5)
	var red: Dictionary = ProceduralSky.sun_of(
		[{"classname": "light_environment", "pitch": "-60", "_light": "128 64 32 200"}])
	_check("the sun keeps the hue of `_light` at full strength",
		red["found"] and red["color"].is_equal_approx(Color(1.0, 0.5, 0.25)))

	# A stand-in for an imported level with its own painted sky.
	var map := Node3D.new()
	var painted := MeshInstance3D.new()
	painted.name = "Sky"
	map.add_child(painted)
	add_child(map)

	var node := ProceduralSky.attach(map,
		[{"classname": "light_environment", "angles": "0 90 0", "pitch": "-45"}])
	_check("the painted sky is hidden, not removed",
		map.get_node_or_null("Sky") == painted and not painted.visible)
	_check("the sky is one node under the level", node.get_parent() == map
		and node.name == "ProceduralSky")

	var sun := node.get_node_or_null("Sun") as DirectionalLight3D
	_check("it has a sun", sun != null)
	if sun != null:
		# A light shines along its own -Z; where it shines from is +Z.
		_check("the sun shines from where the level says",
			_near(sun.global_transform.basis.z, Vector3(1, 1, 0).normalized()))

	var world := node.get_node_or_null("Environment") as WorldEnvironment
	_check("it has an environment whose background is the sky", world != null
		and world.environment.background_mode == Environment.BG_SKY)
	var material := ProceduralSky.material_of(node)
	_check("the sky is drawn by the sky shader", material != null
		and material.shader != null and material.shader.get_mode() == Shader.MODE_SKY)
	if material != null:
		_check("the clouds have their noise",
			material.get_shader_parameter(&"cloud_noise") is NoiseTexture2D)
		var names := PackedStringArray()
		for uniform in material.shader.get_shader_uniform_list():
			names.append(str(uniform["name"]))
		_check("the clouds have a body and a surface to be carved from",
			material.get_shader_parameter(&"shape_noise") is Texture3D
			and material.get_shader_parameter(&"billow_noise") is Texture3D)
		for wanted in ["wind_direction", "wind_speed", "cloud_cover", "cloud_density",
				"cloud_bottom", "cloud_top", "visibility"]:
			_check("the shader can be told `%s`" % wanted, names.has(wanted))

	# The land beyond the level.
	var box := DistantBackdrop.bounds_of(map)
	_check("the level's bounds ignore the sky and are not empty", box.size.length() > 0.0)
	_check("an unknown kind of land is refused", DistantBackdrop.attach(map, "ocean of jam") == null)
	for style in DistantBackdrop.STYLES:
		var land_node := DistantBackdrop.attach(map, style, node)
		var land := land_node.get_node_or_null("Land") as MeshInstance3D if land_node != null else null
		_check("`%s` adds land under the level" % style, land != null and land.mesh != null
			and land_node.get_parent() == map)
		if land == null:
			continue
		var inner: float = land_node.get_meta(&"inner_radius")
		var highest := -INF
		var flat_at_edge := true
		var points: PackedVector3Array = land.mesh.surface_get_arrays(0)[Mesh.ARRAY_VERTEX]
		for point in points:
			highest = maxf(highest, point.y)
			if Vector2(point.x, point.z).length() < inner + 1.0 and absf(point.y) > 1.5:
				flat_at_edge = false
		_check("`%s` starts flat at the level's edge, outside it" % style, flat_at_edge
			and inner >= Vector2(box.size.x, box.size.z).length() * 0.5)
		_check("`%s` rises with distance but not past its stated height" % style,
			highest > 5.0 and highest <= float(DistantBackdrop.STYLES[style]["relief"]) + 0.01)
		var land_material := DistantBackdrop.material_of(land_node)
		_check("`%s` fades into the sky's own horizon colour, lit by its sun" % style,
			land_material != null
			and land_material.get_shader_parameter(&"sun_direction").is_equal_approx(sun.global_transform.basis.z))
		_check("`%s` has a town only when it is one" % style,
			(land_node.get_node_or_null("Town") != null) == (style == "city"))
		var trees := land_node.get_node_or_null("Trees") as MultiMeshInstance3D
		_check("`%s` has trees standing on it, drawn with the land's material" % style,
			trees != null and trees.multimesh.instance_count > 0
			and trees.material_override == land_material)
		var inner_edge: float = land_node.get_meta(&"inner_radius")
		var stray := 0
		# Read from the node's own note of them: without a screen the renderer keeps no trees to ask.
		var spots: PackedVector3Array = trees.get_meta(&"spots", PackedVector3Array()) if trees != null else PackedVector3Array()
		_check("`%s` notes where each of its trees stands" % style,
			trees != null and spots.size() == trees.multimesh.instance_count)
		for at in spots:
			var spot := at - (land_node.get_node("Land") as Node3D).position
			var out := Vector2(spot.x, spot.z).length()
			if out < inner_edge or out > DistantBackdrop.TREE_REACH:
				stray += 1
		_check("`%s` keeps its trees off the level and within reach" % style, stray == 0)
		land_node.free()

	if _failures > 0:
		print("** FAILED ** %d checks" % _failures)
	else:
		print("procedural sky: all checks passed")
	get_tree().quit(1 if _failures > 0 else 0)
