extends RefCounted

## Taking a level's painted light off, so that Godot can light it instead.
##
## A GoldSrc level arrives with its light and shadows computed once, when it was compiled,
## and stored as a picture laid over the walls. That is what makes it look like the game,
## and it is also why nothing you do to a light in Godot changes it. This swaps each lit
## surface for a plain one that takes whatever light the scene has: a sun, lamps, the sky.
##
##   const OwnLighting := preload("res://addons/quebratsk_editor/own_lighting.gd")
##   OwnLighting.unpaint(map)                 # the walls now wait for Godot's light
##   OwnLighting.add_lamps(map, entities)     # one lamp where the level had each of its own
##   OwnLighting.repaint(map)                 # back to the level's own light
##
## Nothing is thrown away: the plain materials are laid over the level's own, which stay in
## the mesh underneath, so `repaint` is exact.
##
## What it cannot bring back by itself: much of the light in these levels never came from a
## lamp entity. It came from textures the compiler was told to treat as glowing (a ceiling
## panel, a screen), and bounced around the room. A level unpainted and given only its lamp
## entities is darker and harsher indoors than it was. Outdoors, under a sun, it holds up.

const SurfaceMaterial := preload("res://addons/quebratsk_editor/surface_material.gd")

const UNITS_TO_METRES := 0.0254
## What `_light` says when it gives no strength.
const DEFAULT_STRENGTH := 200.0


## Lay a plain, Godot-lit material over every surface that carries painted light.
## Returns how many surfaces were changed.
static func unpaint(map: Node) -> int:
	var changed := 0
	for instance in _mesh_instances(map):
		var mesh: Mesh = instance.mesh
		var any := false
		for i in mesh.get_surface_count():
			var painted := mesh.surface_get_material(i) as ShaderMaterial
			if painted == null or SurfaceMaterial.lightmap_of(painted) == null:
				continue
			var plain := StandardMaterial3D.new()
			plain.resource_name = painted.resource_name
			plain.albedo_texture = SurfaceMaterial.texture_of(painted)
			plain.texture_filter = BaseMaterial3D.TEXTURE_FILTER_NEAREST_WITH_MIPMAPS
			# Old walls are matt; anything shinier shows the sun as a white smear on concrete.
			plain.roughness = 1.0
			plain.metallic_specular = 0.0
			# A name starting with "{" is how these levels mark a texture with holes in it
			# (a fence, a grate).
			if painted.resource_name.begins_with("{"):
				plain.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA_SCISSOR
			instance.set_surface_override_material(i, plain)
			any = true
			changed += 1
		if any:
			# A level is only the inside faces of its rooms; seen from the sun those are
			# backs, and a back casts no shadow unless told to.
			instance.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_DOUBLE_SIDED
	return changed


## Take the plain materials off again. Returns how many surfaces went back.
static func repaint(map: Node) -> int:
	var restored := 0
	for instance in _mesh_instances(map):
		for i in instance.mesh.get_surface_count():
			if instance.get_surface_override_material(i) != null:
				instance.set_surface_override_material(i, null)
				restored += 1
		instance.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_ON
	var lamps := map.get_node_or_null("Lamps")
	if lamps != null:
		lamps.queue_free()
	return restored


## One Godot lamp for each `light` and `light_spot` the level has, in its colour and roughly
## its reach, under a "Lamps" node. Lamps that start switched off are left out, as they are
## from the painted light. Returns the node.
##
## Roughly: the compiler's light had no edge, it just faded with distance and bounced; a
## Godot lamp stops at its range. Treat the result as a starting point to adjust by eye.
static func add_lamps(map: Node3D, entities: Array) -> Node3D:
	var lamps := Node3D.new()
	lamps.name = "Lamps"
	for entry in entities:
		var entity: Dictionary = entry
		var kind := str(entity.get("classname", ""))
		if kind != "light" and kind != "light_spot":
			continue
		if not str(entity.get("targetname", "")).is_empty() \
				and (int(str(entity.get("spawnflags", "0"))) & 1) != 0:
			continue
		var origin := str(entity.get("origin", "")).split_floats(" ", false)
		if origin.size() < 3:
			continue
		var colour := Color.WHITE
		var strength := DEFAULT_STRENGTH
		var light := str(entity.get("_light", "")).split_floats(" ", false)
		if light.size() >= 3:
			colour = Color(light[0] / 255.0, light[1] / 255.0, light[2] / 255.0)
		if light.size() >= 4:
			strength = light[3]
		elif light.size() == 1:
			strength = light[0]
		if strength <= 0.0:
			continue

		var lamp := OmniLight3D.new()
		lamp.name = "%s_%d" % [kind, lamps.get_child_count()]
		lamp.light_color = colour
		lamp.light_energy = clampf(strength / DEFAULT_STRENGTH, 0.2, 4.0)
		lamp.omni_range = clampf(strength * UNITS_TO_METRES * 3.0, 3.0, 40.0)
		lamp.omni_attenuation = 1.5
		lamp.position = Vector3(-origin[1], origin[2], -origin[0]) * UNITS_TO_METRES
		lamps.add_child(lamp)
	map.add_child(lamps)
	return lamps


static func _mesh_instances(root: Node) -> Array[MeshInstance3D]:
	var found: Array[MeshInstance3D] = []
	var pending: Array[Node] = [root]
	while not pending.is_empty():
		var node: Node = pending.pop_back()
		var instance := node as MeshInstance3D
		# The painted sky box is a picture of somewhere far away; it takes no light.
		if instance != null and instance.mesh != null and instance.name != &"Sky":
			found.append(instance)
		pending.append_array(node.get_children())
	return found
