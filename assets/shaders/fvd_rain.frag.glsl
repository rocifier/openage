// Copyright 2026 the openage authors. See copying.md for legal info.

#version 330

in vec2 tex_pos;
out vec4 out_col;

uniform float time_seconds;
uniform vec2 viewport_size;

// All drops are generated on the GPU. A rain sheet has a spatially stable
// pseudo-random distribution, translated continuously by gravity and wind.
float rain_hash(vec2 cell) {
	vec3 p = fract(vec3(cell.xyx) * vec3(0.1031, 0.1030, 0.0973));
	p += dot(p, p.yxz + 33.33);
	return fract((p.x + p.y) * p.z);
}

float rain_sheet(vec2 pixel, float column_width, float row_height,
                 float speed, float wind, float lean, float stroke_width,
                 float density, float strength) {
	// Moving the pattern, rather than reseeding it each frame, avoids flicker.
	vec2 rain_pos = vec2(pixel.x - wind * time_seconds - lean * pixel.y,
	                     pixel.y + speed * time_seconds);
	vec2 cell = floor(rain_pos / vec2(column_width, row_height));
	vec2 local = fract(rain_pos / vec2(column_width, row_height));
	float seed = rain_hash(cell);
	float present = step(1.0 - density, rain_hash(cell + 19.71));
	float centre_x = mix(0.12, 0.88, seed);
	float start_y = mix(0.03, 0.49, rain_hash(cell + 36.41));
	float length_y = mix(0.11, 0.31, rain_hash(cell + 5.13));
	float from_head = local.y - start_y;

	// Width is measured in *pixels*, keeping distant drops appropriately thin.
	float distance_x = abs(local.x - centre_x) * column_width;
	float streak = 1.0 - smoothstep(stroke_width, stroke_width + 1.15, distance_x);
	float segment = smoothstep(0.0, 0.028, from_head)
	              * (1.0 - smoothstep(length_y * 0.58, length_y, from_head));
	float variation = mix(0.55, 1.0, rain_hash(cell + 72.23));
	return present * streak * segment * variation * strength;
}

void main() {
	vec2 pixel = tex_pos * viewport_size;

	// Parallax in an orthographic/isometric scene is expressed through
	// thickness, speed, brightness and wind drift. Three independent sheets
	// produce depth without spraying actual entities into the simulation.
	float distant = rain_sheet(pixel, 13.0, 115.0, 390.0, 43.0, 0.08,
	                           0.38, 0.78, 0.16);
	float middle = rain_sheet(pixel, 27.0, 182.0, 780.0, 89.0, 0.10,
	                          0.60, 0.68, 0.24);
	float nearby = rain_sheet(pixel, 49.0, 310.0, 1400.0, 152.0, 0.12,
	                          0.93, 0.58, 0.32);

	// Keep the battlefield readable while taking the warmth out of the
	// afternoon palette. The veil is dark; the streaks catch cold light.
	float drops = distant + middle + nearby;
	float haze = 0.046 + 0.013 * (1.0 - tex_pos.y);
	float alpha = clamp(haze + drops, 0.0, 0.58);
	vec3 wet_air = vec3(0.065, 0.115, 0.145);
	vec3 reflected_light = vec3(0.68, 0.80, 0.88);
	vec3 color = mix(wet_air, reflected_light, min(drops / alpha, 1.0));
	out_col = vec4(color, alpha);
}
