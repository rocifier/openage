// Copyright 2026 the openage authors. See copying.md for legal info.

#version 440

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
	mat4 qt_Matrix;
	float qt_Opacity;
	vec4 teamColor;
	bool teamColorEnabled;
	vec2 portraitSize;
};

layout(binding = 1) uniform sampler2D source;

vec3 rgb_to_hsl(vec3 rgb) {
	float high = max(max(rgb.r, rgb.g), rgb.b);
	float low = min(min(rgb.r, rgb.g), rgb.b);
	float delta = high - low;
	float lightness = 0.5 * (high + low);
	if (delta <= 0.00001) return vec3(0.0, 0.0, lightness);
	float saturation = delta / max(0.00001, 1.0 - abs(2.0 * lightness - 1.0));
	float hue;
	if (high == rgb.r) hue = mod((rgb.g - rgb.b) / delta, 6.0);
	else if (high == rgb.g) hue = ((rgb.b - rgb.r) / delta) + 2.0;
	else hue = ((rgb.r - rgb.g) / delta) + 4.0;
	return vec3(hue / 6.0, saturation, lightness);
}

vec3 hsl_to_rgb(vec3 hsl) {
	float c = (1.0 - abs(2.0 * hsl.z - 1.0)) * hsl.y;
	float section = mod(hsl.x * 6.0, 6.0);
	float x = c * (1.0 - abs(mod(section, 2.0) - 1.0));
	vec3 rgb;
	if      (section < 1.0) rgb = vec3(c, x, 0.0);
	else if (section < 2.0) rgb = vec3(x, c, 0.0);
	else if (section < 3.0) rgb = vec3(0.0, c, x);
	else if (section < 4.0) rgb = vec3(0.0, x, c);
	else if (section < 5.0) rgb = vec3(x, 0.0, c);
	else                    rgb = vec3(c, 0.0, x);
	return rgb + vec3(hsl.z - 0.5 * c);
}

vec3 apply_team_color(vec3 rgb) {
	vec3 source_hsl = rgb_to_hsl(rgb);
	float source_hue_degrees = source_hsl.x * 360.0;
	float hue_difference = abs(source_hue_degrees - 310.0);
	float hue_distance = min(hue_difference, 360.0 - hue_difference);
	float hue_weight = 1.0 - smoothstep(30.0, 35.0, hue_distance);
	float saturation_weight = smoothstep(0.10, 0.30, source_hsl.y);
	float mask = hue_weight * saturation_weight;
	if (mask <= 0.0) return rgb;

	vec3 target = rgb_to_hsl(teamColor.rgb);
	float saturation = target.y * min(source_hsl.y / 0.40, 1.0);
	vec3 replaced = hsl_to_rgb(vec3(target.x, saturation, source_hsl.z));
	return mix(rgb, replaced, mask);
}

void main() {
	vec4 pixel = texture(source, qt_TexCoord0);
	vec3 rgb = pixel.a > 0.0 ? pixel.rgb / pixel.a : vec3(0.0);
	bool shadow = round(pixel.a * 255.0) < 250.0
	           && max(max(rgb.r, rgb.g), rgb.b) <= 8.0 / 255.0;
	if (teamColorEnabled && !shadow) {
		rgb = apply_team_color(rgb);
	}

	vec2 corner = abs(qt_TexCoord0 * portraitSize - 0.5 * portraitSize)
	            - (0.5 * portraitSize - vec2(6.0));
	float distance = length(max(corner, 0.0)) + min(max(corner.x, corner.y), 0.0) - 6.0;
	float coverage = 1.0 - smoothstep(-0.5, 0.5, distance);
	fragColor = vec4(rgb * pixel.a, pixel.a) * coverage * qt_Opacity;
}
