shader_type spatial;
// Lit by hand below, so that the far land fades into exactly the colour the sky has at the
// horizon whatever lamps the level itself carries.
// Trees are flat pictures seen from both sides.
render_mode unshaded, cull_disabled;

// The land far beyond a level: coloured by how high and how steep it is, lit by the sun,
// and washed out by the air the further away it lies, until it is the colour of the sky's
// horizon and the two cannot be told apart. See distant_backdrop.gd.

uniform vec3 low_color : source_color = vec3(0.30, 0.38, 0.20);
uniform vec3 high_color : source_color = vec3(0.36, 0.34, 0.28);
uniform vec3 steep_color : source_color = vec3(0.33, 0.31, 0.29);
uniform vec3 peak_color : source_color = vec3(0.92, 0.94, 0.97);
// Above this share of the full height the peak colour takes over; 2 means never.
uniform float peak_line = 2.0;
// Woods, scrub or whatever darkens the low ground in patches, and how much of it there is.
uniform vec3 patch_color : source_color = vec3(0.13, 0.21, 0.10);
uniform float patches : hint_range(0.0, 1.0) = 0.5;
// Height of the flat land around the level, and how tall the tallest ground is, in metres.
uniform float base_height = 0.0;
uniform float relief = 100.0;

// Pointing at the sun, in world axes.
uniform vec3 sun_direction = vec3(0.3, 0.8, 0.2);
uniform vec3 sun_color : source_color = vec3(1.0, 0.96, 0.88);
// The sky's colour at the horizon, and how many metres of air it takes to wash a thing
// most of the way out to it.
uniform vec3 haze_color : source_color = vec3(0.62, 0.76, 0.92);
uniform float haze_distance = 2600.0;

// One seamless picture of soft blotches, read at several sizes: for where the woods stand,
// for the uneven colour of fields, and for the small roughness of the ground.
uniform sampler2D detail_noise : filter_linear_mipmap, repeat_enable, hint_default_white;
// 1 on the trees that stand on the land and 2 on the buildings of a town, which are drawn
// with this same material so that they take the same sun and the same haze.
instance uniform float canopy = 0.0;

// Photographs of the ground, one layer each for: low ground, high ground, woods seen from
// above, steep ground and peaks. Each is pulled towards the colours above by its tint.
uniform sampler2DArray ground_layers : source_color, filter_linear_mipmap_anisotropic, repeat_enable;
uniform vec3 layer_tint[5];
// A tree photographed from the side, with everything around it see-through.
uniform sampler2D tree_picture : source_color, filter_linear_mipmap, repeat_disable;
uniform vec3 tree_tint = vec3(1.0);
// Four fronts of buildings and, last, a flat roof.
uniform sampler2DArray town_layers : source_color, filter_linear_mipmap_anisotropic, repeat_enable;

varying vec3 world_position;
varying vec3 world_normal;
// On a tree: 0 at its foot and 1 at its top, and a number of its own to tell it from the next.
varying float crown;
varying float tone;
// On a building: where on its wall or roof this is, in metres, and whether it is the roof.
varying vec2 wall;
varying float roof;
varying vec2 picture;

// One layer of ground at `at` metres, `size` metres to the picture. From far off a larger
// copy takes over, so that the same few metres are not seen repeating to the horizon.
vec3 layer(int which, vec2 at, float size, float far) {
	vec3 close_by = texture(ground_layers, vec3(at / size, float(which))).rgb;
	vec3 far_off = texture(ground_layers, vec3(at / (size * 7.3) + vec2(0.37, 0.61), float(which))).rgb;
	return mix(close_by, far_off, smoothstep(size * 5.0, size * 30.0, far)) * layer_tint[which];
}

void vertex() {
	world_position = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
	world_normal = normalize(mat3(MODEL_MATRIX) * NORMAL);
	crown = VERTEX.y + 0.5;
	tone = fract(sin(dot(MODEL_MATRIX[3].xz, vec2(12.9898, 78.233))) * 43758.5453);
	picture = UV;
	vec3 body = VERTEX * vec3(length(MODEL_MATRIX[0].xyz), length(MODEL_MATRIX[1].xyz), length(MODEL_MATRIX[2].xyz));
	roof = step(0.5, NORMAL.y);
	wall = abs(NORMAL.x) > 0.5 ? vec2(body.z, -body.y) : vec2(body.x, -body.y);
	if (roof > 0.5) {
		wall = body.xz;
	}
}

void fragment() {
	vec3 normal = normalize(world_normal);
	vec2 ground = world_position.xz;
	float far = length(world_position - CAMERA_POSITION_WORLD);
	// Small things are only there to be seen close by.
	float close = 1.0 - smoothstep(150.0, 900.0, far);
	vec3 color;

	if (canopy > 1.5) {
		// A building: one of the four fronts on its walls, by a number of its own.
		float front = floor(tone * 3.999);
		vec3 walls = texture(town_layers, vec3(wall / (front == 2.0 ? 20.0 : 13.0), front)).rgb;
		vec3 top = texture(town_layers, vec3(wall / 15.0, 4.0)).rgb;
		color = mix(walls, top, roof) * mix(0.8, 1.1, fract(tone * 7.31));
	} else if (canopy > 0.5) {
		vec4 tree = texture(tree_picture, picture);
		if (tree.a < 0.45) {
			discard;
		}
		// A flat picture has no side that faces the sun: light it as the top of a wood.
		normal = vec3(0.0, 1.0, 0.0);
		color = tree.rgb * tree_tint * mix(0.8, 1.15, tone) * mix(0.8, 1.05, crown);
	} else {
		float tall = clamp((world_position.y - base_height) / max(relief, 1.0), 0.0, 1.0);
		float broad = texture(detail_noise, ground / 2600.0).r;
		float middle = texture(detail_noise, ground / 410.0 + vec2(0.3, 0.7)).r;
		float small = texture(detail_noise, ground / 61.0 + vec2(0.6, 0.2)).r;

		// Woods on the gentler, lower ground, in big stands with ragged edges. The trees in
		// distant_backdrop.gd are planted by this same sum.
		float stands = broad * 0.62 + middle * 0.28 + small * 0.10;
		float edge = 0.72 - patches * 0.3;
		float wooded = smoothstep(edge, edge + 0.05, stands) * (1.0 - smoothstep(0.35, 0.7, tall));

		// The ground is never quite even: lean the surface a little by the blotches, more
		// under trees, so that the sun picks out bumps instead of one smooth sheet.
		vec2 here = ground / 140.0 + vec2(0.11, 0.53);
		float bump = texture(detail_noise, here).r;
		vec2 lean = vec2(texture(detail_noise, here + vec2(0.004, 0.0)).r - bump,
				texture(detail_noise, here + vec2(0.0, 0.004)).r - bump);
		vec3 even = normal;
		normal = normalize(normal - vec3(lean.x, 0.0, lean.y) * mix(3.0, 9.0, wooded));

		color = mix(layer(0, ground, 16.0, far), layer(1, ground, 22.0, far), smoothstep(0.08, 0.55, tall));
		// Fields of slightly different greens.
		color *= 0.8 + 0.4 * texture(detail_noise, ground / 900.0 + vec2(0.7, 0.4)).r;
		color = mix(color, layer(2, ground, 150.0, far) * (0.85 + 0.3 * small), wooded);
		// Rock has to be laid on the slope from the side, or it smears down it.
		float across = even.x * even.x / max(even.x * even.x + even.z * even.z, 0.0001);
		vec3 rock = mix(layer(3, world_position.xy, 45.0, far), layer(3, world_position.zy, 45.0, far), across);
		color = mix(color, rock * (0.85 + 0.3 * middle), smoothstep(0.80, 0.55, even.y));
		color = mix(color, layer(4, ground, 30.0, far), smoothstep(peak_line - 0.08, peak_line + 0.08, tall + (middle - 0.5) * 0.12)
				* smoothstep(0.45, 0.7, even.y));
	}

	float facing = max(dot(normal, normalize(sun_direction)), 0.0);
	// What the sun does not reach is lit by the sky; walls also by the pale streets below them,
	// or a town's shaded sides all turn the blue of the air.
	vec3 shade = canopy > 1.5 ? mix(haze_color, vec3(0.8, 0.78, 0.74), 0.65) * 0.5 : haze_color * 0.30;
	vec3 light = sun_color * facing * 1.25 + shade;

	float washed = 1.0 - exp(-far / haze_distance);
	ALBEDO = mix(color * light, haze_color, washed);
}
