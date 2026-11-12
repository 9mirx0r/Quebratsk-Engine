shader_type sky;

// A sky that is computed instead of painted: a gradient from horizon to zenith, a sun where
// the scene's first DirectionalLight3D points from, and a layer of clouds that drifts with
// the wind and changes shape as it goes.
//
// Nothing here comes from a game's files, so a level saved with it carries no picture that
// belongs to anybody else. See procedural_sky.gd, which builds the nodes around it.

uniform vec3 zenith_color : source_color = vec3(0.10, 0.28, 0.70);
uniform vec3 horizon_color : source_color = vec3(0.62, 0.76, 0.92);
uniform vec3 ground_color : source_color = vec3(0.28, 0.27, 0.25);
uniform vec3 sunset_color : source_color = vec3(1.0, 0.50, 0.22);
uniform float sun_size_degrees : hint_range(0.2, 10.0) = 1.4;

// A seamless noise picture. Three copies of it at different sizes, sliding at different
// speeds, are what make a cloud change shape rather than merely travel.
uniform sampler2D cloud_noise : filter_linear_mipmap, repeat_enable, hint_default_white;
// 0 is a clear sky, 1 is overcast.
uniform float cloud_cover : hint_range(0.0, 1.0) = 0.45;
uniform float cloud_softness : hint_range(0.02, 0.5) = 0.2;
// Bigger numbers make smaller, more numerous clouds.
uniform float cloud_scale : hint_range(0.02, 1.0) = 0.11;
// Which way the wind blows (x is east-west, y is north-south in Godot's ground plane) and
// how fast, in noise tiles per second.
uniform vec2 wind_direction = vec2(1.0, 0.35);
uniform float wind_speed : hint_range(0.0, 0.1) = 0.006;

float cloud_field(vec2 uv, vec2 drift) {
	float n = texture(cloud_noise, uv + drift).r * 0.55;
	n += texture(cloud_noise, uv * 2.03 + drift * 1.7 + vec2(0.37, 0.11)).r * 0.28;
	n += texture(cloud_noise, uv * 4.10 + drift * 2.6 + vec2(0.71, 0.53)).r * 0.17;
	return n;
}

void sky() {
	vec3 dir = normalize(EYEDIR);
	float up = dir.y;
	vec3 sun_dir = LIGHT0_ENABLED ? normalize(LIGHT0_DIRECTION) : normalize(vec3(0.3, 0.8, 0.2));
	vec3 sun_color = LIGHT0_ENABLED ? LIGHT0_COLOR : vec3(1.0, 0.96, 0.88);

	// How high the sun is decides the mood: low sun reddens the horizon and dims the zenith.
	float day = smoothstep(-0.05, 0.35, sun_dir.y);
	float toward_sun = max(dot(dir, sun_dir), 0.0);
	vec3 horizon = mix(mix(horizon_color, sunset_color, 0.35 + 0.5 * pow(toward_sun, 4.0)),
			horizon_color, day);
	vec3 zenith = zenith_color * mix(0.18, 1.0, day);
	vec3 color = mix(horizon, zenith, pow(clamp(up, 0.0, 1.0), 0.5));
	// The air glows around the sun.
	color += sun_color * pow(toward_sun, 8.0) * 0.16;

	float angle = acos(clamp(dot(dir, sun_dir), -1.0, 1.0));
	float radius = radians(sun_size_degrees);
	float disc = 1.0 - smoothstep(radius * 0.85, radius * 1.15, angle);
	vec3 sun = sun_color * (disc * 6.0 + pow(toward_sun, 600.0) * 1.5 + pow(toward_sun, 80.0) * 0.35);

	if (up > 0.0) {
		// The clouds lie on a flat ceiling; dividing by the height is what makes them crowd
		// together towards the horizon, as real ones do.
		vec2 uv = dir.xz / (up + 0.12) * cloud_scale;
		vec2 wind = wind_direction / max(length(wind_direction), 0.0001);
		vec2 drift = -wind * wind_speed * TIME;
		float n = cloud_field(uv, drift);
		float edge = mix(0.78, 0.22, cloud_cover);
		float density = smoothstep(edge, edge + cloud_softness, n);
		density *= smoothstep(0.0, 0.16, up);

		// The same field a step towards the sun: thicker there means this spot is in shade.
		float toward = cloud_field(uv + sun_dir.xz * 0.035, drift);
		float lit = clamp(0.62 + (n - toward) * 5.0 - density * 0.25, 0.0, 1.0);
		vec3 bright = mix(vec3(1.0), sun_color, 0.35) * mix(0.55, 1.05, day);
		vec3 shade = mix(horizon, vec3(0.38, 0.42, 0.50), 0.75) * mix(0.35, 1.0, day);
		vec3 cloud = mix(shade, bright, lit);
		// Thin edges between the eye and the sun light up.
		cloud += sun_color * pow(toward_sun, 12.0) * (1.0 - density) * 0.5;

		color = mix(color + sun * (1.0 - density), cloud, density);
	} else {
		color = mix(horizon, ground_color, smoothstep(0.0, 0.2, -up));
	}
	COLOR = color;
}
