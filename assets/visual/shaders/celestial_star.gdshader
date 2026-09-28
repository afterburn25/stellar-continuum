shader_type canvas_item;
render_mode unshaded, blend_mix;

uniform vec4 star_color : source_color = vec4(1.0, 0.80, 0.47, 1.0);

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float noise(vec2 p) {
    vec2 i = floor(p), f = fract(p); f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1,0)), f.x),
        mix(hash(i + vec2(0,1)), hash(i + vec2(1,1)), f.x), f.y);
}

void fragment() {
    vec2 p = UV * 2.0 - 1.0;
    float radial = length(p);
    float photosphere = 0.455;
    float edge = max(fwidth(radial), 0.001);
    float disc_alpha = 1.0 - smoothstep(photosphere - edge, photosphere + edge, radial);
    float theta = atan(p.y, p.x);
    float slow_phase = TIME * 0.025;
    float streamers = 0.75 + 0.13 * sin(theta * 9.0 + slow_phase)
        + 0.09 * sin(theta * 17.0 - slow_phase * 0.6)
        + 0.06 * sin(theta * 31.0 + sin(theta * 5.0 - slow_phase));
    float corona_distance = max(0.0, radial - photosphere);
    float corona = exp(-corona_distance * 10.0) * streamers
        * (1.0 - smoothstep(0.68, 1.0, radial)) * 0.42;
    float prominence = exp(-corona_distance * 43.0) * 0.32;
    vec2 drift = vec2(TIME * .006, -TIME * .004);
    float convection = noise(p * 11.3 + drift * .15);
    float cells = noise(p * 47.0 + drift + convection * 2.7);
    float grains = noise(p * 103.0 - drift * .7 + cells * 2.1);
    float granules = .70 + grains * .20 + cells * .09 + convection * .07;
    float lanes = 1.0 - smoothstep(.032, .12, abs(grains - .5 + (cells - .5) * .13));
    float spot_field = noise(p * 5.4 + vec2(.71, 2.93) + drift * .025);
    float spot_edge = noise(p * 14.7 - vec2(1.8, .4));
    float penumbra = smoothstep(.73, .88, spot_field) * smoothstep(.33, .68, spot_edge);
    float umbra = smoothstep(.82, .94, spot_field) * smoothstep(.48, .78, spot_edge);
    float limb = sqrt(max(0.0, 1.0 - pow(radial / photosphere, 2.0)));
    // The supplied spectral color identifies warm G/K/F stars without a new material cache key.
    // Blue/white and red stars retain their own palette while Sol receives full amber structure.
    float solar_treatment = smoothstep(.54, .78, star_color.g)
        * (1.0 - smoothstep(.68, .94, star_color.b));
    vec3 spectral_deep = star_color.rgb * vec3(.72, .64, .57);
    vec3 spectral_high = mix(star_color.rgb, vec3(1.0), .30);
    vec3 deep = mix(spectral_deep, vec3(1.0, .21, .014), solar_treatment * .72);
    vec3 gold = mix(star_color.rgb, vec3(1.0, .57, .055), solar_treatment * .70);
    vec3 hot = mix(spectral_high, vec3(1.0, .91, .36), solar_treatment);
    float cell_light = smoothstep(.34, .77, grains * .72 + cells * .28);
    float rim = smoothstep(.78, .985, radial / photosphere) * disc_alpha;
    vec3 core_color = mix(deep, gold, cell_light) * (.74 + limb * .23) * granules
        * (1.0 - penumbra * .30 - umbra * .46) + hot * (lanes * .18 + rim * .58);
    vec3 halo_color = mix(star_color.rgb, vec3(1.0, .28, .025), solar_treatment * .62)
        * (0.62 + prominence);
    float alpha = max(disc_alpha, corona);
    vec3 result = mix(halo_color, core_color, disc_alpha);
    COLOR = vec4(result, alpha) * COLOR;
}
