shader_type spatial;
render_mode blend_mix, depth_draw_always, cull_disabled, specular_schlick_ggx;

// The surface of a func_water brush.
//
// GoldSrc water is a texture whose name begins with a tilde and a scrolling offset, and that
// is the whole of it: no reflection, no refraction, no waves. This is not that, and it is not
// pretending to be — it is the modernisation a project building on top of a level wants,
// where restoring one would leave the original texture alone.
//
// Kept small on purpose. Two scrolling noise fields for the surface, a Fresnel term so it goes
// mirror-like at a glancing angle and clear when looked straight down into, and depth fade at
// the edges so it meets the bank instead of ending in a line.

uniform vec3 shallow : source_color = vec3(0.18, 0.38, 0.36);
uniform vec3 deep : source_color = vec3(0.02, 0.09, 0.12);
uniform float wave_height : hint_range(0.0, 0.4) = 0.06;
uniform float wave_scale = 1.6;
uniform vec2 flow = vec2(0.03, 0.02);
uniform float edge_fade : hint_range(0.05, 4.0) = 0.6;

// Raised by the storm: rain roughens a surface and kills its reflection.
uniform float agitation : hint_range(0.0, 1.0) = 0.55;

// What the scene behind this surface is, for fading the water out where it meets the bank.
// Declared as a uniform because Godot 4 removed the built-in of the same name.
uniform sampler2D depth_behind : hint_depth_texture, filter_linear_mipmap;

float hash(vec2 p) {
	return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float noise(vec2 p) {
	vec2 i = floor(p);
	vec2 f = fract(p);
	f = f * f * (3.0 - 2.0 * f);
	return mix(mix(hash(i), hash(i + vec2(1, 0)), f.x),
	           mix(hash(i + vec2(0, 1)), hash(i + vec2(1, 1)), f.x), f.y);
}

float waves(vec2 p) {
	// Two fields drifting against each other. One alone reads as a moving texture; two
	// crossing read as a surface with something under it.
	return noise(p * wave_scale + flow * TIME * 8.0) * 0.6
	     + noise(p * wave_scale * 2.7 - flow.yx * TIME * 11.0) * 0.4;
}

void vertex() {
	float h = waves(VERTEX.xz);
	VERTEX.y += (h - 0.5) * wave_height;
}

void fragment() {
	// The normal from the slope of the same field the vertices ride, sampled either side.
	vec2 p = (INV_VIEW_MATRIX * vec4(VERTEX, 1.0)).xz;
	float step_size = 0.15;
	float hx = waves(p + vec2(step_size, 0.0)) - waves(p - vec2(step_size, 0.0));
	float hz = waves(p + vec2(0.0, step_size)) - waves(p - vec2(0.0, step_size));
	NORMAL = normalize(mix(NORMAL, normalize(vec3(-hx, 0.4, -hz)), 0.8));

	// Straight down into it is clear; at a glancing angle it turns to sky. That change is
	// most of what makes water read as water rather than as a coloured pane.
	float facing = clamp(dot(normalize(VIEW), NORMAL), 0.0, 1.0);
	float fresnel = pow(1.0 - facing, 3.0);

	ALBEDO = mix(deep, shallow, facing * 0.8);
	// Rain on the surface: rougher, so the reflection breaks up instead of staying a mirror.
	ROUGHNESS = mix(0.02, 0.35, agitation);
	METALLIC = 0.0;
	SPECULAR = 0.6 + fresnel * 0.4;

	// Meets the bank instead of ending in a hard line, by fading where the geometry behind is
	// close to the surface.
	float scene = textureLod(depth_behind, SCREEN_UV, 0.0).r;
	vec4 world = INV_PROJECTION_MATRIX * vec4(SCREEN_UV * 2.0 - 1.0, scene * 2.0 - 1.0, 1.0);
	world.xyz /= world.w;
	float thickness = clamp((-world.z + VERTEX.z) / edge_fade, 0.0, 1.0);

	ALPHA = mix(0.25, 0.92, thickness) * (0.55 + fresnel * 0.45);
}
