extends Node3D

## Does the computed sky put the sun where a level says it is, and does it take the place of
## the level's own painted sky?
##
## Needs no game installed: the "level" here is a bare node with a "Sky" child, and its
## entities are written out by hand. What the sky looks like is for tools/visual_check.py
## and a pair of eyes; this judges the parts that have a right answer.

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
		for wanted in ["wind_direction", "wind_speed", "cloud_cover"]:
			_check("the shader can be told `%s`" % wanted, names.has(wanted))

	if _failures > 0:
		print("** FAILED ** %d checks" % _failures)
	else:
		print("procedural sky: all checks passed")
	get_tree().quit(1 if _failures > 0 else 0)
