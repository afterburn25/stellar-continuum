shader_type spatial;
render_mode blend_mix, depth_prepass_alpha, cull_back, unshaded;

uniform sampler2D surface_map : source_color, filter_linear_mipmap_anisotropic;
uniform vec4 base_color : source_color = vec4(.72, .71, .67, 1.0);
uniform bool mapped = false;
uniform vec3 sun_direction_world = vec3(-.4, .7, -.5);
uniform float sky_visibility : hint_range(0.0, 1.0) = .7;

varying vec3 world_normal;

void vertex() {
    world_normal = normalize((MODEL_MATRIX * vec4(NORMAL, 0.0)).xyz);
}

void fragment() {
    vec3 albedo = mapped ? texture(surface_map, UV).rgb : base_color.rgb;
    float sunlight = max(dot(normalize(world_normal), normalize(sun_direction_world)), 0.0);
    // A small earthshine floor retains the dark limb without flattening the phase terminator.
    float luminance = .025 + sunlight * .975;
    ALBEDO = albedo * luminance * sky_visibility;
    ALPHA = mix(.72, 1.0, sky_visibility);
}
