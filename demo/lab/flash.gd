extends OmniLight3D

## The light of a gunshot.
##
## GoldSrc put a light on a weapon for the instant it fired (the muzzle flash event makes a
## dlight), and it showed on the walls because the engine added it to the level's baked
## light. It was a small, quick thing. This is the same idea with a real light: it reaches
## further, it lights the people in the room as well as the room, and it colours the haze.
##
## One of these is kept by whoever shoots and set off again on every shot.

## How bright at the moment of the shot, and how long it takes to be gone, in seconds.
const PEAK := 1.7
const LASTS := 0.07

## What the weapon in the shooter's own hands gets of it. The flash is a hand's breadth from
## it, and lit by the same light as the room it would be burnt white; it gets a glint.
const ON_THE_HANDS := 0.12
## The layer the shooter's own view model is drawn on, apart from everything else.
const HANDS_LAYER := 2

var _fade: Tween
var _glint: OmniLight3D


func _init() -> void:
	light_color = Color(1.0, 0.86, 0.66)
	light_energy = 0.0
	omni_range = 7.0
	omni_attenuation = 1.6
	light_cull_mask = 0xFFFFF & ~HANDS_LAYER
	# No shadows: it is alive for a few frames, and a shadow map for each of them costs more
	# than the whole of the rest of the shot.
	shadow_enabled = false
	light_volumetric_fog_energy = 0.3
	visible = false


## For the one who sees their own weapon: a much weaker light that falls on it alone.
func light_the_hands() -> void:
	_glint = OmniLight3D.new()
	_glint.light_color = light_color
	_glint.light_energy = 0.0
	_glint.omni_range = 2.0
	_glint.shadow_enabled = false
	_glint.light_volumetric_fog_energy = 0.0
	_glint.light_cull_mask = HANDS_LAYER
	add_child(_glint)


func fire(strength := 1.0) -> void:
	if not is_inside_tree():
		return
	if _fade != null:
		_fade.kill()
	visible = true
	# Never quite the same twice, like the flash itself.
	light_energy = PEAK * strength * randf_range(0.8, 1.15)
	_fade = create_tween()
	_fade.tween_property(self, "light_energy", 0.0, LASTS)
	if _glint != null:
		_glint.light_energy = light_energy * ON_THE_HANDS
		_fade.parallel().tween_property(_glint, "light_energy", 0.0, LASTS)
	_fade.tween_callback(hide)
