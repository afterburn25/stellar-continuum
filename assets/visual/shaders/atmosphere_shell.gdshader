shader_type spatial;
render_mode blend_add, cull_back, unshaded, depth_draw_never;

uniform vec4 atmosphere_color : source_color = vec4(0.42, 0.64, 0.84, 1.0);
uniform float density : hint_range(0.0, 1.0) = 0.15;

void fragment() {
    float facing = max(dot(normalize(NORMAL), normalize(VIEW)), 0.0);
    float rim = pow(1.0 - facing, 3.2);
    float optical_depth = rim * smoothstep(0.0, 0.16, facing);
    ALBEDO = atmosphere_color.rgb * optical_depth;
    EMISSION = atmosphere_color.rgb * optical_depth * 0.32;
    ALPHA = optical_depth * density * 2.0;
}
