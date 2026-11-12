extends RefCounted

## Reading the texture back off a surface, whatever kind of material it got.
##
## A model's surface carries a StandardMaterial3D, whose texture is the `albedo_texture`
## property. A lit map surface carries a ShaderMaterial instead (the texture has to be
## multiplied by the map's baked light, which the standard material cannot do the way the
## game did), and there the texture is a shader parameter of the same name. Anything that
## asks "does this surface have its texture?" has to look in both places, so it asks here.
##
##   const SurfaceMaterial := preload("res://addons/quebratsk_editor/surface_material.gd")
##   var texture := SurfaceMaterial.texture_of(mesh.surface_get_material(i))

## Shader parameter names, as set by the engine's mesh converter.
const ALBEDO := &"albedo_texture"
const LIGHTMAP := &"lightmap_texture"


## The surface's own texture, or null when it has none (or no material at all).
static func texture_of(material: Material) -> Texture2D:
	if material is BaseMaterial3D:
		return (material as BaseMaterial3D).albedo_texture
	if material is ShaderMaterial:
		return (material as ShaderMaterial).get_shader_parameter(ALBEDO) as Texture2D
	return null


## The map's baked light, or null for a surface that is not lit that way.
static func lightmap_of(material: Material) -> Texture2D:
	if material is ShaderMaterial:
		return (material as ShaderMaterial).get_shader_parameter(LIGHTMAP) as Texture2D
	return null
