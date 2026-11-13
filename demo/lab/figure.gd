extends RefCounted

## What a Counter-Strike figure does with a weapon, for whoever has one: the enemies, and the
## player's own body, which nobody sees but its shadow.

const ModelPreset := preload("res://addons/quebratsk_editor/model_preset.gd")

## The game's own muzzle flashes, in the order a weapon's firing event counts them.
const FLASHES := ["sprites/muzzleflash1.spr", "sprites/muzzleflash2.spr",
	"sprites/muzzleflash3.spr"]
## How long a flash is on the screen, in seconds. The game draws it for one frame of a hundred
## a second; a little longer here, so that a slower screen does not miss it altogether.
const FLASH_LASTS := 0.03
## The model event that says "a flash, here": 5001 on the first attachment, and the three
## after it on the others. From the public SDK's list of client events.
const FLASH_EVENT := 5001
## A unit of the game in metres.
const UNIT := 0.0254

## How the game puts a flash on the screen: turned to the eye, and its colour added to what
## is behind it. The adding is done on the numbers as they are shown, which is how the game's
## renderer does it, and not on the amounts of light they stand for, which is what an added
## material does here and leaves the same flash at about half the strength.
const FLASH_SHADER := """
shader_type spatial;
render_mode unshaded, cull_disabled, shadows_disabled, fog_disabled;
uniform sampler2D picture : filter_linear, repeat_disable;
uniform sampler2D behind : hint_screen_texture, filter_nearest, repeat_disable;
uniform float roll = 0.0;
vec3 to_shown(vec3 c) {
	c = clamp(c, 0.0, 1.0);
	return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c));
}
vec3 to_light(vec3 c) {
	return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));
}
void vertex() {
	VERTEX.xy = mat2(vec2(cos(roll), sin(roll)), vec2(-sin(roll), cos(roll))) * VERTEX.xy;
	// Facing the eye, at the size it was given and wherever it hangs.
	MODELVIEW_MATRIX = VIEW_MATRIX * mat4(MAIN_CAM_INV_VIEW_MATRIX[0], MAIN_CAM_INV_VIEW_MATRIX[1],
		MAIN_CAM_INV_VIEW_MATRIX[2], MODEL_MATRIX[3]);
	MODELVIEW_MATRIX = MODELVIEW_MATRIX * mat4(vec4(length(MODEL_MATRIX[0].xyz), 0.0, 0.0, 0.0),
		vec4(0.0, length(MODEL_MATRIX[1].xyz), 0.0, 0.0),
		vec4(0.0, 0.0, length(MODEL_MATRIX[2].xyz), 0.0), vec4(0.0, 0.0, 0.0, 1.0));
	MODELVIEW_NORMAL_MATRIX = mat3(MODELVIEW_MATRIX);
}
void fragment() {
	vec4 lit = texture(picture, UV);
	vec3 add = lit.rgb * lit.a;
	if (max(add.r, max(add.g, add.b)) < 0.004) {
		discard;
	}
	vec3 was = texture(behind, SCREEN_UV).rgb;
	ALBEDO = max(was, to_light(to_shown(was) + add));
}
"""

## The frames of each of FLASHES, read once.
static var _flash_pictures: Array = []
static var _flash_read := false
static var _flash_shader: Shader
## What each weapon model asks for, by its path.
static var _flash_kinds := {}


## Which flash a weapon's own model asks for, and how big: {"sprite", "scale"}.
##
## It is written in the model, on the frame of each firing animation the flash belongs to, as
## a two-digit number: the last digit is which of the three sprites, the first is the size in
## tenths, and no first digit means a half. R_MuzzleFlash in the open Xash3D engine reads it
## exactly so. Read here straight out of the file, by the layout in the SDK's studio.h.
static func flash_kind(lab: Node, model_uri: String) -> Dictionary:
	if _flash_kinds.has(model_uri):
		return _flash_kinds[model_uri]
	var kind := {"sprite": 0, "scale": 0.5, "found": false}
	var bytes: PackedByteArray = lab.vfs.read_file(model_uri) if not model_uri.is_empty() \
		else PackedByteArray()
	if bytes.size() > 244:
		var count := bytes.decode_s32(164)
		var first := bytes.decode_s32(168)
		for i in count:
			var at := first + i * 176
			if at + 176 > bytes.size() or bool(kind["found"]):
				break
			var events := bytes.decode_s32(at + 48)
			var where := bytes.decode_s32(at + 52)
			for e in events:
				var event := where + e * 76
				if event + 76 > bytes.size():
					break
				if bytes.decode_s32(event + 4) != FLASH_EVENT:
					continue
				var written := bytes.slice(event + 12, event + 76)
				var type := written.get_string_from_ascii().to_int()
				kind["sprite"] = (type % 10) % FLASHES.size()
				@warning_ignore("integer_division")
				var tenths := type / 10
				kind["scale"] = 0.5 if tenths == 0 else tenths * 0.1
				kind["found"] = true
				break
	_flash_kinds[model_uri] = kind
	return kind


## An animation with the legs of one and the arms of another.
##
## Counter-Strike keeps them apart: walk, run and idle1 move the legs and leave everything
## above the waist as the model was built, arms out, and ref_aim_ak47 and its kin are the
## body holding a weapon. The game plays both at once. An AnimationPlayer plays one, so the
## two are put together into one the first time they are asked for.
static func layered(moves: AnimationPlayer, skeleton: Skeleton3D, legs: String,
		arms: String) -> String:
	if arms.is_empty() or skeleton == null or moves == null:
		return legs
	var both := legs + "+" + arms
	if moves.has_animation(both):
		return both
	var spine := skeleton.find_bone("Bip01 Spine")
	if spine < 0 or not moves.has_animation(arms) or not moves.has_animation(legs):
		return legs
	var above: Animation = moves.get_animation(arms)
	var made: Animation = moves.get_animation(legs).duplicate(true)
	for t in made.get_track_count():
		var path := made.track_get_path(t)
		var bone := skeleton.find_bone(str(path.get_concatenated_subnames()))
		# The spine and everything that hangs from it.
		var upper := false
		while bone >= 0:
			if bone == spine:
				upper = true
				break
			bone = skeleton.get_bone_parent(bone)
		if not upper:
			continue
		var from := above.find_track(path, made.track_get_type(t))
		if from < 0:
			continue
		for k in range(made.track_get_key_count(t) - 1, -1, -1):
			made.track_remove_key(t, k)
		for k in above.track_get_key_count(from):
			made.track_insert_key(t, above.track_get_key_time(from, k),
				above.track_get_key_value(from, k))
	moves.get_animation_library("").add_animation(both, made)
	return both


## Put the weapon in the hand that holds it, and hand back what was hung there (null when
## there was nothing to hang or nowhere to hang it).
##
## On the right hand's bone, through a BoneAttachment3D, because a weapon parented to the body
## and positioned by hand ends up wherever the body's origin happens to be — which in GoldSrc
## is the centre of the hull rather than the hands, and put every rifle in the sky.
static func hold(lab: Node, skeleton: Skeleton3D, weapon: Dictionary, who: String) -> Node3D:
	if skeleton == null or weapon.is_empty():
		return null
	var hand := -1
	for b in skeleton.get_bone_count():
		var bone := skeleton.get_bone_name(b).to_lower()
		if bone.contains("r hand") or bone.contains("r_hand") or bone.contains("righthand"):
			hand = b
			break
	if hand < 0:
		# No hand to hang it from. Said out loud rather than hung off the root, which is how
		# rifles ended up floating above people's heads.
		print("[figure] %s has no right-hand bone, so it carries nothing" % who)
		return null

	# Not the weapon as its owner sees it, which is a pair of arms holding a gun a hand's
	# breadth from the eye. Every weapon has a second model for how it looks in somebody
	# else's hands: p_ak47 beside v_ak47.
	var seen := str(weapon.get("model", "")).replace("v_", "p_")
	var uri := ModelPreset.resolve(lab.vfs, seen)
	if uri.is_empty():
		print("[figure] %s: no %s, so it carries nothing" % [who, seen])
		return null
	var model: Node3D = lab.importer.load_model(uri, "", PackedStringArray(), {})
	if model == null:
		return null
	for child in model.get_children():
		if child is StaticBody3D:
			model.remove_child(child)
			child.queue_free()

	var socket := BoneAttachment3D.new()
	socket.name = "Held"
	socket.bone_idx = hand
	skeleton.add_child(socket)
	socket.add_child(model)
	# That model is built on a copy of the body's own bones, down the right arm to the hand,
	# and the game draws it with the body's bones in place of its own. Here it hangs from the
	# body's hand, set back by where its own hand is, which comes to the same thing.
	if model is Skeleton3D:
		# Still, as it was built: an animation of its own would move its copy of the arm and
		# the weapon with it, away from the hand it is hung on.
		var moves := model.get_node_or_null("AnimationPlayer")
		if moves != null:
			model.remove_child(moves)
			moves.queue_free()
		(model as Skeleton3D).reset_bone_poses()
		var own := (model as Skeleton3D).find_bone(skeleton.get_bone_name(hand))
		if own >= 0:
			model.transform = (model as Skeleton3D).get_bone_global_rest(own).affine_inverse()
	return model


## Where the end of the barrel is, in the axes of whatever `model` hangs from.
##
## These models name no muzzle, so it is taken from the shape: a weapon is longest along its
## barrel, and the hand has it by the end that is not the muzzle.
static func barrel_end(model: Node3D) -> Vector3:
	var box := AABB()
	var any := false
	var parts: Array = model.find_children("*", "MeshInstance3D", true, false)
	if model is MeshInstance3D:
		parts.append(model)
	for part in parts:
		var instance := part as MeshInstance3D
		if instance.mesh == null:
			continue
		var up: Node = instance
		var within := Transform3D.IDENTITY
		while up != model and up != null:
			within = (up as Node3D).transform * within
			up = up.get_parent()
		var piece: AABB = (model.transform * within) * instance.mesh.get_aabb()
		box = piece if not any else box.merge(piece)
		any = true
	if not any:
		return Vector3.ZERO
	var axis := box.get_longest_axis_index()
	var near := box.get_center()
	var far := near
	near[axis] = box.position[axis]
	far[axis] = box.end[axis]
	return far if far.length() > near.length() else near


## The flash at the end of a barrel: the game's own sprite, at the game's own size, added
## over the picture. Hidden until `flare` is called on it.
##
## `kind` is what `flash_kind` read from the weapon's model.
static func muzzle_flash(lab: Node, kind: Dictionary) -> Sprite3D:
	if not _flash_read:
		_flash_read = true
		for named in FLASHES:
			var frames: Array = []
			var uri := ModelPreset.resolve(lab.vfs, str(named))
			if not uri.is_empty():
				var read: Dictionary = lab.importer.load_sprite(uri)
				for frame in read.get("frames", []):
					if frame is Texture2D:
						frames.append(frame)
			_flash_pictures.append(frames)
	var which := int(kind.get("sprite", 0))
	if which >= _flash_pictures.size() or (_flash_pictures[which] as Array).is_empty():
		return null

	var flash := Sprite3D.new()
	flash.name = "MuzzleFlash"
	flash.texture = _flash_pictures[which][0]
	flash.set_meta("sprite", which)
	flash.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	# A sprite is drawn a unit to the pixel, times the scale it was asked for.
	flash.pixel_size = UNIT * float(kind.get("scale", 0.5))
	if _flash_shader == null:
		_flash_shader = Shader.new()
		_flash_shader.code = FLASH_SHADER
	var look := ShaderMaterial.new()
	look.shader = _flash_shader
	look.set_shader_parameter("picture", flash.texture)
	flash.material_override = look
	flash.visible = false
	return flash


## Show `flash` for an instant: another frame of its sprite, turned as the game turns it.
static func flare(flash: Sprite3D) -> void:
	if flash == null or not flash.is_inside_tree():
		return
	var which := int(flash.get_meta("sprite", 0))
	var frames: Array = _flash_pictures[which] if which < _flash_pictures.size() else []
	if frames.is_empty():
		return
	var picture: Texture2D = frames[randi() % frames.size()]
	flash.texture = picture
	var look := flash.material_override as ShaderMaterial
	if look != null:
		look.set_shader_parameter("picture", picture)
		# The first sprite is the long one for rifles and is only rocked a little, so it
		# stays along the barrel; the others are round and are turned any way at all.
		look.set_shader_parameter("roll",
			deg_to_rad(randf_range(0.0, 20.0) if which == 0 else randf_range(0.0, 359.0)))
	flash.visible = true
	# Counted against the shot it was shown for, so a shot fired before this one has gone
	# out does not put the new one out early.
	var shot := int(flash.get_meta("shot", 0)) + 1
	flash.set_meta("shot", shot)
	flash.get_tree().create_timer(FLASH_LASTS).timeout.connect(func() -> void:
		if is_instance_valid(flash) and int(flash.get_meta("shot", 0)) == shot:
			flash.visible = false)
