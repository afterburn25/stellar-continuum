shader_type spatial;
render_mode blend_mix, depth_prepass_alpha, cull_disabled, unshaded;

// UV.x is the physical radial distance along an actual annular mesh, rather than a
// texture cutout. Fine bands and the broad Cassini division remain sharp at any distance.
varying float radial_band;

void vertex() {
    radial_band = UV.x;
}

void fragment() {
    float r = radial_band;
    float fine = sin(r * 270.0) * .5 + .5;
    float medium = sin(r * 52.0 + sin(r * 13.0) * 1.7) * .5 + .5;
    float broad = sin(r * 13.0 + .7) * .5 + .5;
    float cassini = smoothstep(.505, .518, r) - smoothstep(.542, .557, r);
    float inner_gap = smoothstep(.070, .085, r) - smoothstep(.108, .126, r);
    float edge = smoothstep(.008, .030, r) * (1.0 - smoothstep(.970, .996, r));
    float opacity = (.31 + medium * .27 + broad * .16 + fine * .10) * edge;
    opacity *= 1.0 - cassini * .94;
    opacity *= 1.0 - inner_gap * .72;
    vec3 shadowed = vec3(.20, .18, .15);
    vec3 cream = mix(vec3(.48, .42, .31), vec3(.92, .78, .50), medium * .62 + fine * .20);
    // The innermost A/B bands are darker and give a useful geometric shadow approximation
    // across the limb-facing portion when a real shadow map is unavailable at map scale.
    vec3 colour = mix(shadowed, cream, smoothstep(.12, .33, r));
    ALBEDO = colour * .92;
    ROUGHNESS = .82;
    SPECULAR = .10;
    ALPHA = opacity;
}
