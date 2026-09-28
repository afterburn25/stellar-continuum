shader_type canvas_item;
render_mode unshaded, blend_mix;

uniform sampler2D surface_texture : source_color, filter_linear_mipmap, repeat_enable;
uniform bool has_source = false;
uniform bool known_surface = false;
uniform bool is_gas = false;
uniform bool is_ocean = false;
uniform vec4 source_disc = vec4(0.0);
uniform vec4 surface_color : source_color = vec4(0.22, 0.28, 0.32, 1.0);
uniform vec4 atmosphere_color : source_color = vec4(0.20, 0.48, 0.85, 1.0);
uniform float atmosphere_strength : hint_range(0.0, 0.5) = 0.0;
uniform vec3 light_direction = vec3(-0.7, -0.3, 0.64);
uniform float surface_seed = 0.0;

// Bounded seeded value noise prevents procedural rocky worlds from resolving into
// regular sine dots or latitude stripes. The fixed three octaves stay decorative
// and deterministic; they do not represent surveyed terrain or resources.
float hash13(vec3 cell) {
    vec3 q = fract(cell * vec3(0.1031, 0.11369, 0.13787));
    q += dot(q, q.yzx + 19.19 + surface_seed * 0.071);
    return fract((q.x + q.y) * q.z);
}

float value_noise(vec3 p) {
    vec3 cell = floor(p);
    vec3 local = fract(p);
    local = local * local * (3.0 - 2.0 * local);
    float n000 = hash13(cell + vec3(0.0, 0.0, 0.0));
    float n100 = hash13(cell + vec3(1.0, 0.0, 0.0));
    float n010 = hash13(cell + vec3(0.0, 1.0, 0.0));
    float n110 = hash13(cell + vec3(1.0, 1.0, 0.0));
    float n001 = hash13(cell + vec3(0.0, 0.0, 1.0));
    float n101 = hash13(cell + vec3(1.0, 0.0, 1.0));
    float n011 = hash13(cell + vec3(0.0, 1.0, 1.0));
    float n111 = hash13(cell + vec3(1.0, 1.0, 1.0));
    return mix(mix(mix(n000, n100, local.x), mix(n010, n110, local.x), local.y),
        mix(mix(n001, n101, local.x), mix(n011, n111, local.x), local.y), local.z);
}

float terrain_variation(vec3 normal) {
    vec3 seed_offset = vec3(surface_seed * 0.037, surface_seed * -0.053, surface_seed * 0.071);
    float continental = value_noise(normal * 2.15 + seed_offset);
    float regional = value_noise(normal * 6.40 + seed_offset.yzx * 1.7);
    float mineral = value_noise(normal * 17.30 + seed_offset.zxy * 2.3);
    float ridge = 1.0 - abs(regional * 2.0 - 1.0);
    return 0.54 + continental * 0.27 + ridge * 0.18 + mineral * 0.09;
}

void fragment() {
    vec2 p = (UV * 2.0 - 1.0) * 1.08;
    float radius = length(p);
    float edge = max(fwidth(radius), 0.0008);
    // One continuous coverage function for the opaque globe. Previously its alpha
    // reached zero INSIDE the limb and jumped back up at the atmospheric shell,
    // producing a detached outline over bright space backgrounds.
    float coverage = 1.0 - smoothstep(1.0 - edge, 1.0 + edge, radius);
    vec3 light = normalize(light_direction);
    vec3 limb_normal = vec3(p / max(radius, 0.001), 0.05);
    float sunrim = smoothstep(-0.2, 0.45, dot(limb_normal, light));
    float air = exp(-max(radius - 1.0, 0.0) * 80.0)
        * (1.0 - smoothstep(1.035, 1.078, radius)) * sunrim * atmosphere_strength * 0.65;
    vec3 lit = vec3(0.0);
    if (coverage > 0.0) {
        vec2 globe = p / max(1.0, radius);
        vec3 normal = vec3(globe, sqrt(max(0.0, 1.0 - dot(globe, globe))));
        vec3 albedo = surface_color.rgb;
        if (has_source) {
            vec2 source_uv;
            if (source_disc.z > 0.0) {
                // Preserve the observed hemisphere of globe photographs, including
                // the documented LEFT natural-color Uranus comparison disc.
                // Stay half a source texel inside the documented photographic limb,
                // so bilinear sampling cannot pull in the photograph's matte.
                vec2 source_pixels = vec2(textureSize(surface_texture, 0));
                vec2 safe_radii = max(source_disc.zw - 0.5 / source_pixels, vec2(0.001));
                source_uv = source_disc.xy + globe * safe_radii;
            } else {
                source_uv = vec2(0.5 + atan(normal.x, normal.z) / 6.28318530718,
                    0.5 + asin(clamp(normal.y, -1.0, 1.0)) / 3.14159265359);
            }
            vec4 source_sample = texture(surface_texture, source_uv);
            albedo = source_sample.rgb;
            coverage *= source_sample.a;
        } else if (known_surface) {
            // Decorative class-level shading only; never used for unknown worlds.
            float variation;
            if (is_gas) {
                // Gas giants retain broad latitude bands, with bounded irregular drift.
                float band_drift = value_noise(normal * 5.0 + vec3(surface_seed)) * 7.0;
                variation = 0.72 + 0.28 * sin(normal.y * 65.0 + band_drift);
            } else {
                variation = terrain_variation(normal);
                if (is_ocean) {
                    // Ocean classes keep calmer large-scale tonal shifts than mineral worlds.
                    variation = mix(0.82, variation, 0.42);
                }
            }
            albedo *= variation;
        }

        float incidence = dot(normal, light);
        float daylight = smoothstep(-0.10, 0.16, incidence);
        float diffuse = max(incidence, 0.0);
        float illumination = 0.025 + daylight * (0.17 + diffuse * 0.91);
        // Disc photographs already contain real observed illumination. Preserve it,
        // just as the 3D Earth renderer does; spherical maps receive stellar lighting.
        lit = albedo * (has_source && source_disc.z > 0.0 ? 1.0 : illumination);

        // Atmosphere uses only confirmed visual classes; the caller reserves an outer halo.
        float fresnel = pow(1.0 - normal.z, 3.0);
        float illuminated_rim = smoothstep(-0.2, 0.45, incidence);
        lit += atmosphere_color.rgb * atmosphere_strength * fresnel * illuminated_rim * 1.4;
        lit = mix(lit, atmosphere_color.rgb * 0.30,
            atmosphere_strength * fresnel * daylight * 0.25);

        // Subtle reflected-light glint on photographed blue ocean pixels.
        if (is_ocean && has_source && source_disc.z == 0.0) {
            float water = smoothstep(0.015, 0.12, albedo.b - max(albedo.r, albedo.g * 0.90));
            vec3 half_vector = normalize(light + vec3(0.0, 0.0, 1.0));
            float reflection = pow(max(dot(normal, half_vector), 0.0), 82.0);
            lit += vec3(0.72, 0.83, 0.92) * reflection * water * daylight * 0.22;
        }
    }
    // Composite the thin atmosphere behind the opaque globe in straight alpha.
    // Airless worlds have exactly zero alpha beyond the antialiased surface edge.
    float atmosphere_coverage = air * (1.0 - coverage);
    float alpha = coverage + atmosphere_coverage;
    vec3 rgb = (lit * coverage + atmosphere_color.rgb * atmosphere_coverage) / max(alpha, 0.00001);
    COLOR = vec4(rgb, alpha) * COLOR;
}
