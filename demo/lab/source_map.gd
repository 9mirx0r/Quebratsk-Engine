extends RefCounted

## A Source level, as the engine reads it, made ready for the lab to walk.
##
## The reading is the engine's (VBSPParser, through the importer): the level's faces and
## sculpted ground with their baked light, its entities and the props it places by the
## thousand. What is left here is what belongs to the lab rather than to the format: which
## brush entities are shown and stood on, and loading the props a few at a time so the level
## can be walked while they arrive.
##
## Not read yet: the light baked into each prop's corners (they are lit by the level's light
## for moving things instead), water as water, overlays and decals, and cubemaps.

## Brush entities that are drawn and stood on where the level put them. The others (triggers,
## volumes of rain or dust, portals, occluders) are the level's bookkeeping, not its shape.
const SOLID_CLASSES := ["func_brush", "func_wall", "func_detail", "func_breakable",
	"func_breakable_surf", "func_door", "func_door_rotating", "func_lod", "func_physbox",
	"func_illusionary", "func_tracktrain", "func_movelinear", "func_rotating", "func_button"]

## Drawn, and walked straight through.
const GHOST_CLASSES := ["func_illusionary"]

var entities: Array = []
var spawns: Array[Vector3] = []
var report := {}

## What lights the props by the level's own light for moving things, when there is one:
## the lab's floor light, given use_ambient().
var light: Node = null

var _importer
var _vfs
var _uri := ""


## Build the level. `path` is the .bsp on disk; it is opened through what is mounted, so its
## textures and models are looked for beside it first.
func build(path: String, importer, vfs) -> Node3D:
	_importer = importer
	_vfs = vfs
	_uri = _find(path)
	if _uri.is_empty():
		# Not under anything mounted: mount the folder above its maps folder, which is where
		# the files packed with the level were laid out.
		var top := path.get_base_dir()
		if top.get_file().to_lower() == "maps":
			top = top.get_base_dir()
		vfs.mount_directory("sourcemap", top)
		_uri = _find(path)
	if _uri.is_empty():
		return null
	# The files packed inside the level (its own materials, cubemaps and models).
	report["packed files"] = vfs.mount_container("mappak_" + path.get_file().get_basename(), path)

	var map: Node3D = importer.load_map(_uri)
	if map == null:
		return null
	entities = importer.load_map_entities(_uri)
	for entity in entities:
		if str(entity.get("classname", "")).begins_with("info_player_") and entity.has("position"):
			spawns.append(entity["position"])
	report["entities"] = entities.size()
	_sort_brushes(map)
	return map


## The mounted file that is this level.
func _find(path: String) -> String:
	var wanted := "maps/" + path.get_file().to_lower()
	var hit: Dictionary = _vfs.find_files(path.get_file().get_basename(),
		PackedStringArray(["bsp"]), PackedStringArray(), PackedStringArray(), 64)
	for file in hit.get("files", []):
		if str(file).to_lower().ends_with(wanted):
			return str(file)
	return ""


## Hide the brush entities that are not part of the level's shape, and let the ghosts be
## walked through.
func _sort_brushes(map: Node3D) -> void:
	var by_model := {}
	for entity in entities:
		var model := str(entity.get("model", ""))
		if model.begins_with("*"):
			by_model["brush_" + model.substr(1)] = str(entity.get("classname", ""))
	var shown := 0
	var hidden := 0
	for child in map.get_children():
		if not child.name.begins_with("brush_"):
			continue
		var cls: String = by_model.get(str(child.name), "")
		var keep := cls in SOLID_CLASSES
		if not keep:
			(child as Node3D).visible = false
			hidden += 1
		else:
			shown += 1
		if not keep or cls in GHOST_CLASSES:
			for body in child.get_children():
				if body is StaticBody3D:
					child.remove_child(body)
					body.free()
	report["brush entities shown"] = shown
	report["brush entities hidden"] = hidden


## The props, a few models at a time so the level can be walked while they arrive: there
## are hundreds of models and reading them all in one go is half a minute of nothing.
func build_props(root: Node3D, tree: SceneTree) -> void:
	var props: Array = _importer.load_map_props(_uri)
	if props.is_empty():
		return

	# Every place a model stands, by model and skin.
	var stands := {}
	var names := {}
	for prop in props:
		var key := "%s/%d" % [prop["model"], prop["skin"]]
		names[prop["model"]] = true
		if not stands.has(key):
			stands[key] = {"model": prop["model"], "skin": prop["skin"], "at": [], "solid": []}
		(stands[key]["at"] as Array).append(prop["transform"])
		(stands[key]["solid"] as Array).append(prop["solid"])

	var holder := Node3D.new()
	holder.name = "Props"
	root.add_child(holder)
	var body := StaticBody3D.new()
	body.name = "PropsBody"
	root.add_child(body)

	var built := 0
	var failed := {}
	var started := Time.get_ticks_msec()
	for key in stands:
		if Time.get_ticks_msec() - started > 40:
			await tree.process_frame
			started = Time.get_ticks_msec()
		var stand: Dictionary = stands[key]
		var uri := _resolve(str(stand["model"]))
		var model: Node3D = null
		if not uri.is_empty():
			model = _importer.load_model(uri, "", PackedStringArray(), {}, int(stand["skin"]))
		if model == null:
			failed[str(stand["model"])] = true
			continue
		var parts: Array = model.find_children("*", "MeshInstance3D", true, false)
		if model is MeshInstance3D:
			parts.append(model)
		for part in parts:
			var source := part as MeshInstance3D
			if source.mesh == null:
				continue
			# Where the mesh sits inside its own model, without ever entering the tree.
			var inside := Transform3D.IDENTITY
			var walk: Node = source
			while walk != null and walk != model:
				if walk is Node3D:
					inside = (walk as Node3D).transform * inside
				walk = walk.get_parent()
			inside = model.transform * inside

			var places: Array = stand["at"]
			var over: Material = null
			for surface in source.mesh.get_surface_count():
				if source.get_surface_override_material(surface) != null:
					over = source.get_surface_override_material(surface)
					break
			var shape: Shape3D = null
			for i in places.size():
				var where: Transform3D = (places[i] as Transform3D) * inside
				# One node each rather than one for the lot, so what is behind the eye or
				# far off is not drawn: a level has thousands and shows a few dozen.
				var drawn := MeshInstance3D.new()
				drawn.mesh = source.mesh
				drawn.transform = where
				drawn.material_override = over
				# Its shadow is already in the level's baked light.
				drawn.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
				drawn.visibility_range_end = 90.0
				holder.add_child(drawn)
				if light != null:
					light.light_prop(drawn)
				if stand["solid"][i]:
					if shape == null:
						shape = source.mesh.create_trimesh_shape()
					var collider := CollisionShape3D.new()
					collider.shape = shape
					collider.transform = where
					body.add_child(collider)
			built += places.size()
		model.free()

	report["props"] = props.size()
	report["props built"] = built
	report["prop models"] = names.size()
	report["prop models not read"] = failed.keys()
	print("[source map] props: %d placed of %d, %d model(s), %d not read"
		% [built, props.size(), names.size(), failed.size()])


func _resolve(path: String) -> String:
	var hit: Dictionary = _vfs.find_files(path.get_file().get_basename(),
		PackedStringArray([path.get_extension()]), PackedStringArray(), PackedStringArray(), 64)
	for file in hit.get("files", []):
		if str(file).to_lower().ends_with(path.to_lower()):
			return str(file)
	return ""
