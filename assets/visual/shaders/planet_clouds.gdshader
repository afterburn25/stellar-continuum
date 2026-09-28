shader_type spatial;
render_mode blend_mix, depth_draw_never;
uniform float seed=0.;
uniform bool mapped=false;
uniform sampler2D cloud_map : filter_linear_mipmap_anisotropic, repeat_enable;
varying vec3 cloud_point;
float hash(vec3 p){return fract(sin(dot(p,vec3(127.1,311.7,74.7))+seed)*43758.5453);}
float noise(vec3 p){
    vec3 i=floor(p),f=fract(p); f=f*f*(3.-2.*f);
    return mix(mix(mix(hash(i),hash(i+vec3(1,0,0)),f.x),mix(hash(i+vec3(0,1,0)),hash(i+vec3(1,1,0)),f.x),f.y),
        mix(mix(hash(i+vec3(0,0,1)),hash(i+vec3(1,0,1)),f.x),mix(hash(i+vec3(0,1,1)),hash(i+vec3(1,1,1)),f.x),f.y),f.z);
}
void vertex(){cloud_point=normalize(VERTEX);}
void fragment(){
    vec3 p=cloud_point; p.x+=TIME*.00004;
    float weather=noise(p*12.)*.45+noise(p*30.)*.25+noise(p*75.)*.18+noise(p*170.)*.12;
    ALBEDO=vec3(.94,.97,1.);
    ROUGHNESS=1.;
    ALPHA=mapped ? smoothstep(.14,.86,texture(cloud_map,UV+vec2(TIME*.000002,0)).r)*.78
        : smoothstep(.49,.73,weather)*.6;
}
