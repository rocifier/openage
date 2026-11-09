// Copyright 2026 the openage authors. See copying.md for legal info.

#version 330

in vec2 tex_pos;

out vec4 out_col;

uniform sampler2D skybox;
uniform sampler2D terrain;
uniform sampler2D world;
uniform sampler2D hud;

vec3 scene_color(vec2 uv) {
	uv = clamp(uv, vec2(0.0), vec2(1.0));
	vec4 color = texture(skybox, uv);
	vec4 layer = texture(terrain, uv);
	color.rgb = mix(color.rgb, layer.rgb, layer.a);
	layer = texture(world, uv);
	color.rgb = mix(color.rgb, layer.rgb, layer.a);
	layer = texture(hud, uv);
	return mix(color.rgb, layer.rgb, layer.a);
}

void main() {
	vec2 pixel = 1.5 / vec2(textureSize(skybox, 0));
	vec3 color = 4.0 * scene_color(tex_pos);
	color += scene_color(tex_pos + vec2(-pixel.x, -pixel.y));
	color += scene_color(tex_pos + vec2(pixel.x, -pixel.y));
	color += scene_color(tex_pos + vec2(-pixel.x, pixel.y));
	color += scene_color(tex_pos + vec2(pixel.x, pixel.y));
	out_col = vec4(color / 8.0, 1.0);
}
