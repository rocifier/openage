// Copyright 2026 the openage authors. See copying.md for legal info.

#version 330

in vec2 tex_pos;

out vec4 out_col;

uniform sampler2D tex;

vec3 sample_color(vec2 uv) {
	return texture(tex, clamp(uv, vec2(0.0), vec2(1.0))).rgb;
}

void main() {
	vec2 pixel = 1.5 / vec2(textureSize(tex, 0));
	vec3 color = 4.0 * sample_color(tex_pos);
	color += sample_color(tex_pos + vec2(-pixel.x, -pixel.y));
	color += sample_color(tex_pos + vec2(pixel.x, -pixel.y));
	color += sample_color(tex_pos + vec2(-pixel.x, pixel.y));
	color += sample_color(tex_pos + vec2(pixel.x, pixel.y));
	out_col = vec4(color / 8.0, 1.0);
}
