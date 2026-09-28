shader_type sky;
uniform float seed = 0.0;
uniform float air_density : hint_range(0.0,1.0) = 0.0;
uniform vec4 sky_color : source_color = vec4(.08,.25,.55,1);
uniform vec4 horizon_color : source_color = vec4(.6,.75,.85,1);
float hash(vec3 p) { return fract(sin(dot(p,vec3(127.1,311.7,74.7)) + seed)*43758.5453); }
float noise(vec3 p) {
    vec3 i=floor(p), f=fract(p); f=f*f*(3.0-2.0*f);
    return mix(mix(mix(hash(i),hash(i+vec3(1,0,0)),f.x),mix(hash(i+vec3(0,1,0)),hash(i+vec3(1,1,0)),f.x),f.y),
        mix(mix(hash(i+vec3(0,0,1)),hash(i+vec3(1,0,1)),f.x),mix(hash(i+vec3(0,1,1)),hash(i+vec3(1,1,1)),f.x),f.y),f.z);
}
void sky() {
    vec3 d=normalize(EYEDIR);
    vec3 cell=floor(d*700.0);
    float star=step(.9985,hash(cell))*pow(max(0.,1.-length(fract(d*700.0)-.5)*2.),5.);
    float horizon=pow(1.-max(d.y,0.),3.);
    vec3 air=mix(sky_color.rgb,horizon_color.rgb,horizon);
    float clouds=noise(d*14.)*.55+noise(d*31.)*.28+noise(d*77.)*.17;
    clouds=smoothstep(.58,.78,clouds)*smoothstep(.02,.2,d.y);
    air=mix(air,vec3(.93,.95,.98),clouds*.62);
    COLOR=mix(vec3(.001,.002,.005)+vec3(star*.85),air,air_density);
}
