// Copyright 2026 the openage authors. See copying.md for legal info.

#version 330

in vec2 tex_pos;

out vec4 out_col;

uniform sampler2D blurred_scene;
uniform vec2 viewport_size;
uniform vec4 panel_rect;
uniform float panel_radius;

vec3 sample_color(vec2 uv) {
	return texture(blurred_scene, clamp(uv, vec2(0.0), vec2(1.0))).rgb;
}

void main() {
	vec2 panel_size = panel_rect.zw * viewport_size;
	vec2 local = (tex_pos - panel_rect.xy) * viewport_size;
	float radius = min(panel_radius, 0.5 * min(panel_size.x, panel_size.y));
	vec2 corner = abs(local - panel_size * 0.5) - (panel_size * 0.5 - vec2(radius));
	float distance = length(max(corner, vec2(0.0)))
	                 + min(max(corner.x, corner.y), 0.0) - radius;
	float alpha = 1.0 - smoothstep(-1.0, 1.0, distance);
	if (alpha <= 0.0) {
		discard;
	}

	vec2 pixel = 1.0 / vec2(textureSize(blurred_scene, 0));
	vec3 color = sample_color(tex_pos + vec2(-pixel.x, 0.0));
	color += sample_color(tex_pos + vec2(pixel.x, 0.0));
	color *= 2.0;
	color += sample_color(tex_pos + vec2(-2.0 * pixel.x, 0.0));
	color += sample_color(tex_pos + vec2(2.0 * pixel.x, 0.0));
	out_col = vec4(color / 6.0, alpha);
}
