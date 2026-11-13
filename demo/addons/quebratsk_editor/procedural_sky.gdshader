shader_type sky;
// The clouds are worked out on a smaller picture; this asks for that picture to exist.
render_mode use_half_res_pass;

// A sky that is computed instead of painted: a gradient from horizon to zenith, a sun where
// the scene's first DirectionalLight3D points from, and clouds that have thickness. They are
// walked through a step at a time, so they have lit tops, dark bellies and bright edges
// against the sun, they crowd together and fade into haze towards the horizon, and they
// drift and change shape with the wind.
//
// Nothing here comes from a game's files, so a level saved with it carries no picture that
// belongs to anybody else. See procedural_sky.gd, which builds the nodes around it.

uniform vec3 zenith_color : source_color = vec3(0.10, 0.28, 0.70);
uniform vec3 horizon_color : source_color = vec3(0.62, 0.76, 0.92);
uniform vec3 ground_color : source_color = vec3(0.28, 0.27, 0.25);
uniform vec3 sunset_color : source_color = vec3(1.0, 0.50, 0.22);
uniform float sun_size_degrees : hint_range(0.2, 10.0) = 1.4;

// Where clouds are and are not, seen from far above: a seamless picture many kilometres
// across. It is what gives a sky fields of cloud and open stretches instead of an even
// sprinkle.
uniform sampler2D cloud_noise : filter_linear_mipmap, repeat_enable, hint_default_white;
// The body of the clouds (soft lumps) and their cauliflower surface (round billows), both
// seamless in all three directions.
uniform sampler3D shape_noise : filter_linear, repeat_enable;
uniform sampler3D billow_noise : filter_linear, repeat_enable;

// 0 is a clear sky, 1 is overcast.
uniform float cloud_cover : hint_range(0.0, 1.0) = 0.45;
// How thick the stuff of a cloud is. Higher is more solid and darker underneath.
uniform float cloud_density : hint_range(0.0, 4.0) = 1.0;
// How ragged the edges are.
uniform float cloud_detail : hint_range(0.0, 1.0) = 0.45;
// The layer the clouds live in, in metres above the ground.
uniform float cloud_bottom : hint_range(200.0, 6000.0) = 1400.0;
uniform float cloud_top : hint_range(400.0, 9000.0) = 3600.0;
// Metres across one repeat of each noise. Bigger numbers make bigger clouds.
uniform float cloud_scale : hint_range(1000.0, 40000.0) = 9000.0;
uniform float weather_scale : hint_range(5000.0, 200000.0) = 60000.0;
// How far one can see before the air swallows things, in metres.
uniform float visibility : hint_range(5000.0, 200000.0) = 55000.0;
// Which way the wind blows (x is east-west, y is north-south in Godot's ground plane) and
// how fast, in metres per second. Real wind up there is 5 to 30.
uniform vec2 wind_direction = vec2(1.0, 0.35);
uniform float wind_speed : hint_range(0.0, 200.0) = 18.0;
// Thin streaks of ice cloud far above the others. 0 is none.
uniform float high_cloud : hint_range(0.0, 1.0) = 0.3;

const float PLANET_RADIUS = 6371000.0;

float remap(float v, float lo, float hi, float new_lo, float new_hi) {
	return new_lo + (v - lo) / (hi - lo) * (new_hi - new_lo);
}

// How far along a ray from inside a sphere of radius `r` (centred on the planet) its
// surface is. `origin_height` is the eye's distance from the centre.
float shell_distance(float origin_height, float dir_up, float r) {
	float b = origin_height * dir_up;
	return -b + sqrt(max(b * b - origin_height * origin_height + r * r, 0.0));
}

// How much cloud there is at a point. `p` is metres from the eye's ground point, `h` is 0
// at the bottom of the layer and 1 at its top. `fine` adds the ragged edge, which the
// steps towards the sun do without. `blur` (0 to 1) drops the billows, for far away, where
// they are smaller than a dot of the picture and would only make it flicker.
float density_at(vec3 p, float h, bool fine, float blur) {
	vec2 wind = wind_direction / max(length(wind_direction), 0.0001);
	vec3 blown = p - vec3(wind.x, 0.0, wind.y) * wind_speed * TIME;

	float weather = texture(cloud_noise, blown.xz / weather_scale).r;
	float cover = clamp(cloud_cover + (weather - 0.5) * 1.1, 0.0, 1.0);

	// Flat underneath, where the air is just cold enough, and domed on top; taller where
	// the weather is heavier.
	float ceiling = mix(0.4, 1.0, clamp(weather * 1.3 * (0.5 + cloud_cover), 0.0, 1.0));
	// Some clouds tower and their neighbours do not.
	ceiling *= mix(0.55, 1.0, smoothstep(0.3, 0.6, texture(shape_noise, blown / (cloud_scale * 2.7)).r));
	// A cloud needs more and more noise to exist the higher one goes, so each one is widest
	// low down and narrows to a rounded crown, and where the noise is strongest it towers.
	float rise = smoothstep(0.0, ceiling, h);
	float needed = mix(1.0 - cover, 1.05, rise * rise * rise);

	// Read more slowly upwards than sideways: what is cloud at the bottom is then still
	// cloud a good way up, and it stands as a tower instead of lying as a slab.
	vec3 at = blown / cloud_scale * vec3(1.0, 0.45, 1.0);
	float lumps = texture(shape_noise, at).r;
	float billows = texture(billow_noise, at * 2.0 + vec3(0.31, 0.17, 0.59)).r;
	float body = mix(lumps, mix(billows, 0.5, blur), 0.4);

	// Dense almost at once inside the edge, and cut off flat underneath.
	float d = clamp((body - needed) / mix(0.12, 0.45, blur), 0.0, 1.0) * smoothstep(0.0, 0.07, h);
	if (fine && d > 0.0) {
		// The small billows rise a little as they are carried along, which is what makes
		// a cloud boil instead of slide.
		vec3 small = blown / (cloud_scale * 0.17) + vec3(0.0, -TIME * 0.012, 0.0);
		float ragged = texture(billow_noise, small).r;
		d = clamp(remap(d, (1.0 - ragged) * cloud_detail * 0.5 * (1.0 - blur), 1.0, 0.0, 1.0), 0.0, 1.0);
	}
	return d;
}

// A cheap stand-in for how a droplet throws light: mostly onwards, a little back.
float phase(float cos_angle, float g) {
	float g2 = g * g;
	return (1.0 - g2) / pow(1.0 + g2 - 2.0 * g * cos_angle, 1.5);
}

// Walk through the cloud layer along `dir`. Returns the light the clouds send to the eye
// (rgb) and how much of what is behind them still gets through (a).
vec4 march_clouds(vec3 dir, vec3 sun_dir, vec3 sun_light, vec3 sky_light, vec3 ground_light,
		vec3 haze, float jitter, int most_steps) {
	if (dir.y <= 0.005) {
		return vec4(0.0, 0.0, 0.0, 1.0);
	}
	float eye = PLANET_RADIUS + 2.0;
	float near = shell_distance(eye, dir.y, PLANET_RADIUS + cloud_bottom);
	float far = shell_distance(eye, dir.y, PLANET_RADIUS + cloud_top);
	// Short steps where the way through the layer is short, and never so long that a cloud
	// could fall between two of them; what lies past the last step is deep in the haze.
	int steps = int(clamp((far - near) / 110.0, 20.0, float(most_steps)));
	far = min(far, near + float(steps) * 230.0);
	if (exp(-near / visibility) < 0.01) {
		return vec4(0.0, 0.0, 0.0, 1.0);
	}

	// Thin enough that one step never swallows all the light: otherwise each ray shows
	// only the first spot it happens to land on, and neighbours disagree.
	float extinction = 0.009 * cloud_density;
	// Sunlight is judged more harshly than the view, which is what carves a cloud into lit
	// faces and shadowed hollows instead of an even white.
	float shadowing = 0.028 * cloud_density;
	float step_length = (far - near) / float(steps);
	float cos_sun = dot(dir, sun_dir);
	float glow = mix(phase(cos_sun, 0.72), phase(cos_sun, -0.2), 0.45);

	vec3 light = vec3(0.0);
	float through = 1.0;
	// The steps sit on shells at fixed distances from the eye, not at fixed distances from
	// where this ray enters the layer: that entry point jumps by more than a step from one
	// row of the picture to the next near the horizon, and the rows would not agree.
	float t = (floor(near / step_length) + jitter) * step_length;
	if (t < near) {
		t += step_length;
	}
	for (int i = 0; i < steps; i++) {
		vec3 p = dir * t;
		// The ground curves away, so height is not simply p.y.
		float height = length(p + vec3(0.0, eye, 0.0)) - PLANET_RADIUS;
		float h = (height - cloud_bottom) / (cloud_top - cloud_bottom);
		// One dot of the picture covers more and more cloud with distance.
		float blur = smoothstep(4000.0, 14000.0, t);
		float d = density_at(vec3(p.x, height, p.z), h, true, blur);
		if (d > 0.001) {
			// How much cloud lies between this spot and the sun.
			float shade = 0.0;
			float reach = 70.0 + t * 0.012;
			float along = 0.0;
			for (int j = 0; j < 5; j++) {
				along += reach;
				vec3 q = p + sun_dir * along;
				float q_height = length(q + vec3(0.0, eye, 0.0)) - PLANET_RADIUS;
				float qh = (q_height - cloud_bottom) / (cloud_top - cloud_bottom);
				if (qh >= 0.0 && qh <= 1.0) {
					shade += density_at(vec3(q.x, q_height, q.z), qh, false, blur) * reach;
				}
				reach *= 1.9;
			}
			float depth = shade * shadowing * mix(1.0, 0.45, blur);
			// Light that has bounced around inside reaches deeper than light that came
			// straight, so the dark side of a cloud is grey, not black.
			float direct = exp(-depth);
			float bounced = exp(-depth * 0.14) * 0.30;
			vec3 lit = sun_light * (direct * glow + bounced);
			// Less of the sky reaches the underside and the hollows than the crown.
			float open = clamp(h * 1.4, 0.0, 1.0);
			lit += mix(ground_light * 0.7, sky_light, open) * mix(0.45, 1.0, direct) * mix(0.6, 1.0, 1.0 - d);
			// Far clouds take the colour of the air in front of them.
			lit = mix(haze, lit, exp(-t / visibility));

			// Far steps are long; thinned, so that no single one decides the whole dot.
			float passes = exp(-d * extinction * mix(1.0, 0.4, blur) * step_length);
			light += through * lit * (1.0 - passes);
			through *= passes;
			if (through < 0.01) {
				through = 0.0;
				break;
			}
		}
		t += step_length;
	}
	return vec4(light, through);
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

	if (AT_HALF_RES_PASS) {
		// The clouds are the costly part, so they are worked out on a picture half as wide
		// and half as tall and laid over the full one below.
		vec3 sun_light = mix(sunset_color, mix(vec3(1.0), sun_color, 0.4), day) * mix(0.5, 1.25, day);
		vec3 sky_light = mix(zenith, horizon, 0.45) * 0.75 + vec3(0.12) * day;
		vec3 ground_light = mix(horizon, ground_color, 0.5) * 0.5;
		// Each ray starts a different fraction of a step in, so the steps do not line up
		// into bands. (This particular scramble has no streaks along a row or a column.)
		// The four dots of each little square share one scramble and take a quarter of a
		// step each, and are averaged at the end: four times the steps for the same work.
		vec2 odd = mod(floor(FRAGCOORD.xy), 2.0);
		float scramble = fract(52.9829189 * fract(dot(floor(FRAGCOORD.xy) - odd, vec2(0.06711056, 0.00583715))));
		float jitter = (odd.x * 2.0 + odd.y * 3.0 - odd.x * odd.y * 4.0 + scramble) * 0.25;
		// Near the horizon a cloud's flat underside is seen almost edge on, and one row of
		// the picture can fall just under it while the next falls just inside. Nudging each
		// ray by a fraction of a dot turns those hard rows into a soft blend.
		vec2 nudge = fract(jitter * vec2(7.31, 13.77) + vec2(0.37, 0.71)) - 0.5;
		vec3 ray = normalize(dir + dFdx(dir) * nudge.x + dFdy(dir) * nudge.y);
		vec4 clouds = march_clouds(ray, sun_dir, sun_light, sky_light, ground_light, horizon,
				jitter, AT_CUBEMAP_PASS ? 20 : 84);
		// Each dot knows how its neighbour differs from it, which is enough to pull all
		// four of a square towards their common average. Only part of the way: all of it
		// would trade the speckle for little square steps along every edge.
		vec2 side = 1.0 - 2.0 * odd;
		vec4 pair = clouds + 0.5 * side.x * dFdx(clouds);
		// Far off, where the steps are long and the clouds have no edges left to keep, all of it.
		float distant = smoothstep(7000.0, 18000.0,
				shell_distance(PLANET_RADIUS + 2.0, max(dir.y, 0.005), PLANET_RADIUS + cloud_bottom));
		clouds = mix(clouds, pair + 0.5 * side.y * dFdy(pair), mix(0.55, 1.0, distant));
		COLOR = clouds.rgb;
		ALPHA = clouds.a;
	} else {
		vec3 color = mix(horizon, zenith, pow(clamp(up, 0.0, 1.0), 0.5));
		// The air glows around the sun.
		color += sun_color * pow(toward_sun, 8.0) * 0.16;

		float angle = acos(clamp(dot(dir, sun_dir), -1.0, 1.0));
		float radius = radians(sun_size_degrees);
		float disc = 1.0 - smoothstep(radius * 0.85, radius * 1.15, angle);
		vec3 sun = sun_color * (disc * 6.0 + pow(toward_sun, 600.0) * 1.5 + pow(toward_sun, 80.0) * 0.35);

		if (up > 0.0) {
			// Ice cloud nine kilometres up, combed into streaks along the wind. It is so
			// thin that it needs no walking through: a picture of it is enough.
			float ice = shell_distance(PLANET_RADIUS + 2.0, up, PLANET_RADIUS + 9000.0);
			vec2 along = wind_direction / max(length(wind_direction), 0.0001);
			vec2 over = dir.xz * ice - along * wind_speed * 1.5 * TIME;
			vec2 combed = vec2(dot(over, along) / 52000.0, dot(over, vec2(-along.y, along.x)) / 21000.0);
			float streaks = texture(cloud_noise, combed).r * 0.6
					+ texture(cloud_noise, over / 17000.0 + vec2(0.37, 0.11)).r * 0.4;
			float fields = texture(cloud_noise, over / 150000.0 + vec2(0.5, 0.2)).r;
			float veil = smoothstep(0.45, 0.85, streaks) * smoothstep(0.62 - high_cloud * 0.3, 0.85 - high_cloud * 0.3, fields)
					* exp(-ice / visibility * 1.2);
			color = mix(color, mix(horizon, vec3(1.0), 0.45 + 0.45 * day), veil * 0.45);

			vec4 clouds = HALF_RES_COLOR;
			color = (color + sun) * clouds.a + clouds.rgb;
		} else {
			color = mix(horizon, ground_color, smoothstep(0.0, 0.2, -up));
		}
		COLOR = color;
	}
}
