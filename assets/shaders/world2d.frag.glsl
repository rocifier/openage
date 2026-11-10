#version 330

in vec2 vert_uv;

layout(location=0) out vec4 col;

uniform sampler2D tex;
uniform bool cbits = false;

// FVD team-colour artwork convention:
// Paint the recolourable fabric, banners, trim, etc. magenta (HSL hue
// 280–340 degrees, saturation >= 30%). Edge pixels blend between 275–345
// degrees and 10–30% saturation to avoid ugly anti-aliased outlines.
// This uses one sprite texture for all players: no tinted texture copies.
// Set only for entities with a valid player owner.
uniform bool team_color_enabled = false;
uniform vec4 team_color = vec4(1.0);
// Below 1 for things remembered in explored fog rather than seen now.
uniform float brightness = 1.0;

// position (top left corner) and size: (x, y, width, height)
uniform vec4 tile_params;

vec2 uv = vec2(
	vert_uv.x * tile_params.z + tile_params.x,
	vert_uv.y * tile_params.w + tile_params.y
);

const float shadow_rgb_max = 8.0 / 255.0;

bool is_shadow(vec4 value, int alpha) {
	return alpha < 250
	    && max(max(value.r, value.g), value.b) <= shadow_rgb_max;
}

// Convert displayed (sRGB-encoded) sprite colours to HSL for *artistic*
// hue selection. No texture preprocessing or change to the alpha channel.
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
	vec3 source = rgb_to_hsl(rgb);
	float source_hue_degrees = source.x * 360.0;
	float hue_difference = abs(source_hue_degrees - 300.0);
	float hue_distance = min(hue_difference, 360.0 - hue_difference);
	// Full replacement within 280–340 degrees, feather to 275–345.
	float hue_weight = 1.0 - smoothstep(20.0, 45.0, hue_distance);
	float saturation_weight = smoothstep(0.20, 0.30, source.y);
	float mask = hue_weight * saturation_weight;
	if (mask <= 0.0) return rgb;

	vec3 target = rgb_to_hsl(team_color.rgb);
	// Preserve painted lightness. Shaded source pixels approach the target
	// saturation smoothly, while player colours such as grey stay desaturated.
	float saturation = target.y * min(source.y / 0.40, 1.0);
	vec3 replaced = hsl_to_rgb(vec3(target.x, saturation, source.z));
	return mix(rgb, replaced, mask);
}

void main() {
	vec4 tex_val = texture(tex, uv);
	int alpha = int(round(tex_val.a * 255));
	// Texture export noise must not write depth through transparent sprite padding.
	// Keep this cutoff synchronized with ALPHA_MASK_CUTOFF in texture_data.h.
	if (alpha <= 20) {
		discard;
	}
	// Translucent effects and dark shadow pixels belong behind opaque sprites.
	// Dense rendered shadows can be more than 50% opaque, so alpha alone does
	// not identify them. Values 250 and above include team-color markers.
	gl_FragDepth = alpha < 128 || is_shadow(tex_val, alpha)
	             ? 1.0
	             : gl_FragCoord.z;
	if (!cbits) {
		// Preserve transparency, depth sorting, dimmed fog, and near-black
		// painted shadows. Anti-aliased magenta edges are still recoloured.
		vec3 rgb = tex_val.rgb;
		if (team_color_enabled && !is_shadow(tex_val, alpha)) {
			rgb = apply_team_color(rgb);
		}
		col = vec4(rgb * brightness, tex_val.a);
		return;
	}
	switch (alpha) {
		case 254:
			col = vec4(1.0f, 0.0f, 0.0f, 1.0f);
			break;
		case 252:
			col = vec4(0.0f, 1.0f, 0.0f, 1.0f);
			break;
		case 250:
			col = vec4(0.0f, 0.0f, 1.0f, 1.0f);
			break;
		default:
			col = tex_val;
			break;
	}
	col.rgb *= brightness;
}
