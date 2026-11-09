// Copyright 2026 the openage authors. See copying.md for legal info.

#version 330

layout(location=0) in vec2 position;

uniform vec4 panel_rect;

out vec2 tex_pos;

void main() {
	tex_pos = panel_rect.xy + (position * 0.5 + 0.5) * panel_rect.zw;
	gl_Position = vec4(tex_pos * 2.0 - 1.0, 0.0, 1.0);
}
