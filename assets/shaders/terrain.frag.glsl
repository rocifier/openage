#version 330

in vec2 tex_pos;
in vec2 grid_pos;
in vec3 world_pos;

layout(location=0) out vec4 out_col;

uniform sampler2D tex;
uniform bool show_grid;

// Fog is a single nearest-filtered texel per tile (se, ne):
// 0 = never explored, 128 = explored, 255 = visible.
// The gameplay fog bitsets remain hard-edged. Only terrain brightness is
// interpolated; hidden units and selection still obey exact tile visibility.
uniform sampler2D fog;
uniform bool fog_enabled;
uniform float fog_map_size;
// Width of the visual transition across a tile boundary, measured in tiles.
// 0 restores hard edges; 0.55 is the default FVD softness.
uniform float fog_edge_width;

// Dry, yellowed grass over a square (the FVD Hunter tower's no-build zone),
// feathered into the surrounding terrain. Purely cosmetic.
// Centre is in (se, ne) tile coordinates, like tile_pos below.
uniform bool dry_zone_enabled;
uniform vec2 dry_zone_centre;
// Distance from the centre to the square's edge, in tiles.
uniform float dry_zone_half_size;
// Width of the fade into the surrounding grass, in tiles.
uniform float dry_zone_feather;

// Read a discrete fog tile without changing the shared GPU texture's filtering.
// Clamp to the map edges so fog never wraps around the opposite side.
float tile_fog_brightness(ivec2 tile) {
    ivec2 last = ivec2(int(fog_map_size) - 1);
    float level = texelFetch(fog, clamp(tile, ivec2(0), last), 0).r;
    return level > 0.75 ? 1.0 : (level > 0.25 ? 0.45 : 0.0);
}

float terrain_fog_brightness(vec2 tile_pos) {
    if (fog_edge_width <= 0.0) {
        return tile_fog_brightness(ivec2(floor(tile_pos)));
    }

    // A slight static wobble keeps long reveal boundaries from looking like
    // rulers. Its displacement is small compared with the blend width.
    float wave = sin(tile_pos.x * 5.3 + tile_pos.y * 1.7)
               * sin(tile_pos.y * 4.7 - tile_pos.x * 2.3);
    vec2 softened_pos = tile_pos + 0.055 * vec2(wave, -wave);

    // Fog texels represent *tile centres*, not corners. Interpolate between
    // adjacent centres but keep flat plateaus around each centre. This makes
    // the transition straddle the actual tile edge (rather than darkening
    // every point throughout the tile, as ordinary linear filtering would).
    vec2 at_centres = softened_pos - vec2(0.5);
    ivec2 base = ivec2(floor(at_centres));
    vec2 fraction = fract(at_centres);
    float half_width = 0.5 * clamp(fog_edge_width, 0.0, 1.0);
    vec2 blend = smoothstep(vec2(0.5 - half_width),
                            vec2(0.5 + half_width), fraction);

    float lower_left = tile_fog_brightness(base);
    float lower_right = tile_fog_brightness(base + ivec2(1, 0));
    float upper_left = tile_fog_brightness(base + ivec2(0, 1));
    float upper_right = tile_fog_brightness(base + ivec2(1, 1));

    return mix(mix(lower_left, lower_right, blend.x),
               mix(upper_left, upper_right, blend.x), blend.y);
}

vec3 rgb_to_hsl(vec3 c) {
    float high = max(max(c.r, c.g), c.b);
    float low = min(min(c.r, c.g), c.b);
    float l = 0.5 * (high + low);
    float d = high - low;
    if (d < 1e-5) {
        return vec3(0.0, 0.0, l);
    }
    float s = l > 0.5 ? d / (2.0 - high - low) : d / (high + low);
    float h;
    if (high == c.r) {
        h = (c.g - c.b) / d + (c.g < c.b ? 6.0 : 0.0);
    }
    else if (high == c.g) {
        h = (c.b - c.r) / d + 2.0;
    }
    else {
        h = (c.r - c.g) / d + 4.0;
    }
    return vec3(h / 6.0, s, l);
}

float hue_to_rgb(float p, float q, float t) {
    t = fract(t);
    if (t < 1.0 / 6.0) return p + (q - p) * 6.0 * t;
    if (t < 0.5) return q;
    if (t < 2.0 / 3.0) return p + (q - p) * (2.0 / 3.0 - t) * 6.0;
    return p;
}

vec3 hsl_to_rgb(vec3 hsl) {
    if (hsl.y < 1e-5) {
        return vec3(hsl.z);
    }
    float q = hsl.z < 0.5 ? hsl.z * (1.0 + hsl.y) : hsl.z + hsl.y - hsl.z * hsl.y;
    float p = 2.0 * hsl.z - q;
    return vec3(hue_to_rgb(p, q, hsl.x + 1.0 / 3.0),
                hue_to_rgb(p, q, hsl.x),
                hue_to_rgb(p, q, hsl.x - 1.0 / 3.0));
}

// The same recolour as ImageMagick's `-modulate 112,60,86` on the grass:
// 112% lightness, 60% saturation, hue turned 25.2 degrees toward yellow/red.
vec3 dry_grass(vec3 c) {
    vec3 hsl = rgb_to_hsl(c);
    hsl.x = fract(hsl.x - 25.2 / 360.0);
    hsl.y = clamp(hsl.y * 0.60, 0.0, 1.0);
    hsl.z = clamp(hsl.z * 1.12, 0.0, 1.0);
    return hsl_to_rgb(hsl);
}

// 1 inside the dry square, fading to 0 across dry_zone_feather tiles
// outside its edge.
float dry_zone_weight(vec2 tile_pos) {
    vec2 offset = abs(tile_pos - dry_zone_centre);
    float distance = max(offset.x, offset.y);
    // A slight static wobble so the fade doesn't read as a ruled line.
    float wave = sin(tile_pos.x * 1.9 + tile_pos.y * 0.7)
               * sin(tile_pos.y * 2.3 - tile_pos.x * 1.1);
    distance += 0.3 * wave;
    return 1.0 - smoothstep(dry_zone_half_size - 0.15,
                            dry_zone_half_size + max(dry_zone_feather, 0.01),
                            distance);
}

void main()
{
    vec4 tex_val = texture(tex, tex_pos);
    if (show_grid) {
        vec2 distance_to_line = abs(fract(grid_pos - 0.5) - 0.5) / fwidth(grid_pos);
        float line = 1.0 - min(min(distance_to_line.x, distance_to_line.y), 1.0);
        tex_val.rgb = mix(tex_val.rgb, vec3(0.08, 0.12, 0.06), line * 0.55);
    }
    // World space is (se, up, -ne).
    vec2 tile_pos = vec2(world_pos.x, -world_pos.z);
    if (dry_zone_enabled) {
        float weight = dry_zone_weight(tile_pos);
        if (weight > 0.0) {
            tex_val.rgb = mix(tex_val.rgb, dry_grass(tex_val.rgb), weight);
        }
    }
    if (fog_enabled) {
        // Do not change unit visibility: this interpolation is strictly a
        // cosmetic terrain effect.
        tex_val.rgb *= terrain_fog_brightness(tile_pos);
    }
    out_col = tex_val;
}
