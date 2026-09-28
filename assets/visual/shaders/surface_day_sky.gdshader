shader_type sky;

// Grounded atmosphere only: it deliberately contains no astronomical background.
uniform vec4 sky_top : source_color = vec4(.09,.24,.42,1.);
uniform vec4 sky_horizon : source_color = vec4(.53,.70,.78,1.);
uniform vec4 ground_horizon : source_color = vec4(.34,.38,.33,1.);
uniform vec4 ground_bottom : source_color = vec4(.10,.13,.11,1.);
uniform float cloud_amount : hint_range(0.0, 1.0) = 0.28;

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1,311.7))) * 43758.5453); }
float noise(vec2 p) {
    vec2 i=floor(p), f=fract(p); f=f*f*(3.-2.*f);
    return mix(mix(hash(i),hash(i+vec2(1.,0.)),f.x),mix(hash(i+vec2(0.,1.)),hash(i+vec2(1.,1.)),f.x),f.y);
}
void sky() {
    vec3 d=normalize(EYEDIR);
    float upward=clamp(d.y,0.,1.);
    float sky_gradient=pow(1.-upward,.58);
    vec3 air=mix(sky_top.rgb,sky_horizon.rgb,sky_gradient);
    // Broad, low-frequency cloud veils keep daylight from reading as a flat blue card.
    vec2 wind=d.xz/max(.18,upward+.18);
    float veil=noise(wind*2.1+vec2(.7,2.4))*.58+noise(wind*5.7+vec2(8.1,1.3))*.42;
    veil=smoothstep(.58,.76,veil)*smoothstep(.035,.25,upward);
    air=mix(air,vec3(.88,.93,.96),veil*cloud_amount);
    float below=1.0-smoothstep(-.34,0.,d.y);
    vec3 ground=mix(ground_horizon.rgb,ground_bottom.rgb,below);
    COLOR=mix(ground,air,smoothstep(-.035,.025,d.y));
}
