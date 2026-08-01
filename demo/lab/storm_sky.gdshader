shader_type sky;

// A storm that moves, over a backdrop that does not.
//
// The panorama underneath is a photograph: its ridges, its mist and the green-gold break on
// the horizon are better than anything generated here, and they are also the half of a sky
// that never needs to move. What a photograph cannot do is change, so the clouds overhead
// scroll, and a discharge lights them from inside.
//
// Layered rather than replaced, and the split is deliberate. Procedural mountains are a much
// larger job than procedural clouds for a much smaller return, so the horizon stays painted
// and the weather is computed.
//
// Nothing here is a restoration. A GoldSrc map declares a sky by name and this is not it, so
// it is an option a project chooses rather than something an import does on its own.

// The painted sky, and the same sky lit from within by a discharge. Cross-fading between two
// photographs of one scene is what makes a flash read as the storm lighting up rather than as
// the sky being replaced.
uniform sampler2D panorama : source_color, filter_linear;
uniform sampler2D panorama_flash : source_color, filter_linear;
uniform bool has_flash_panorama = false;

// 0 at rest, 1 at the peak of a strike. Driven by the map's own thunder.
uniform float flash : hint_range(0.0, 1.0) = 0.0;

// Where the cloud layer begins, as a height above the horizon. Below this the panorama is
// left alone: that is where its mountains are.
uniform float cloud_horizon : hint_range(0.0, 0.6) = 0.06;
uniform float cloud_blend : hint_range(0.01, 0.5) = 0.22;

uniform float cloud_cover : hint_range(0.0, 1.0) = 0.62;
uniform float cloud_scale = 2.4;
uniform vec2 wind = vec2(0.012, 0.004);
uniform float cloud_opacity : hint_range(0.0, 1.0) = 0.7;

uniform vec3 cloud_dark : source_color = vec3(0.10, 0.11, 0.14);
uniform vec3 cloud_light : source_color = vec3(0.55, 0.58, 0.66);
uniform vec3 flash_colour : source_color = vec3(0.82, 0.86, 1.0);

// Sun or moon: a direction and a colour, so a map can be run at any hour.
uniform vec3 sun_direction = vec3(0.3, 0.7, 0.4);
uniform vec3 sun_colour : source_color = vec3(0.9, 0.88, 0.8);
uniform float sun_size : hint_range(0.0, 0.2) = 0.0;

float hash(vec3 p) {
	p = fract(p * 0.3183099 + vec3(0.1, 0.2, 0.3));
	p *= 17.0;
	return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}

// Value noise, smoothed. Cheap and enough: these are clouds, not a fluid simulation.
float noise(vec3 p) {
	vec3 i = floor(p);
	vec3 f = fract(p);
	f = f * f * (3.0 - 2.0 * f);
	return mix(
		mix(mix(hash(i + vec3(0, 0, 0)), hash(i + vec3(1, 0, 0)), f.x),
		    mix(hash(i + vec3(0, 1, 0)), hash(i + vec3(1, 1, 0)), f.x), f.y),
		mix(mix(hash(i + vec3(0, 0, 1)), hash(i + vec3(1, 0, 1)), f.x),
		    mix(hash(i + vec3(0, 1, 1)), hash(i + vec3(1, 1, 1)), f.x), f.y), f.z);
}

// Five octaves. Storm cloud is structure at every scale at once, and three is visibly smooth
// while seven costs more than the difference is worth on a sky.
float fbm(vec3 p) {
	float sum = 0.0;
	float weight = 0.5;
	for (int i = 0; i < 5; i++) {
		sum += weight * noise(p);
		p *= 2.03;
		weight *= 0.5;
	}
	return sum;
}

void sky() {
	vec3 dir = normalize(EYEDIR);

	// The panorama, sampled the way an equirectangular image is meant to be: longitude from
	// the horizontal angle, latitude from the height.
	vec2 uv = vec2(atan(dir.x, -dir.z) / (2.0 * PI) + 0.5, acos(clamp(dir.y, -1.0, 1.0)) / PI);
	vec3 base = texture(panorama, uv).rgb;

	// The lit variant, if there is one. Without it a flash can only brighten what is there,
	// which reads as an exposure change rather than as a discharge.
	if (has_flash_panorama) {
		base = mix(base, texture(panorama_flash, uv).rgb, flash);
	} else {
		base = mix(base, base * 2.2 + flash_colour * 0.25, flash);
	}

	// Above the horizon only. Written as a weight rather than an early exit because the sky
	// processor function is not allowed to return.
	float above = smoothstep(cloud_horizon, cloud_horizon + cloud_blend, dir.y);

	// Projected onto a plane far overhead rather than onto the sphere, so cloud stretches
	// toward the horizon the way a real ceiling does instead of wrapping evenly around.
	vec3 p = dir / max(dir.y, 0.06);
	p.xz += wind * TIME * 6.0;
	p *= cloud_scale;

	float density = fbm(p);
	// A second, slower layer drifting the other way. One layer of noise reads as a texture;
	// two moving against each other read as weather.
	density = mix(density, fbm(p * 0.45 - vec3(wind.y, 0.0, wind.x) * TIME * 3.0), 0.45);
	density = smoothstep(1.0 - cloud_cover, 1.0 - cloud_cover + 0.42, density);

	// Lit from underneath at rest, and from inside during a strike. That difference is the
	// whole effect: a discharge does not shine on cloud, it happens within it.
	float depth = fbm(p * 2.1 + vec3(0.0, TIME * 0.05, 0.0));
	vec3 cloud = mix(cloud_dark, cloud_light, depth);
	cloud = mix(cloud, flash_colour, flash * (0.35 + 0.65 * depth));

	// A sun or moon behind the cloud rather than in front of it.
	if (sun_size > 0.001) {
		float disc = smoothstep(1.0 - sun_size, 1.0 - sun_size * 0.5,
			dot(dir, normalize(sun_direction)));
		base += sun_colour * disc * (1.0 - density * 0.85);
	}

	COLOR = mix(base, cloud, density * above * cloud_opacity);
}
