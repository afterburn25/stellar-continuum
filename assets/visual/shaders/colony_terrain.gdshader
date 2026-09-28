shader_type spatial;
render_mode diffuse_burley;

varying vec3 terrain_position;
varying vec3 terrain_normal;
uniform vec3 terrain_low : source_color = vec3(0.105,0.17,0.08);
uniform vec3 terrain_high : source_color = vec3(0.28,0.32,0.15);
uniform vec3 terrain_exposed_low : source_color = vec3(0.22,0.16,0.10);
uniform vec3 terrain_exposed_high : source_color = vec3(0.39,0.30,0.19);
uniform float terrain_seed = 0.0;
uniform float terrain_roughness = 0.97;
uniform sampler2D terrain_detail : source_color, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D terrain_normal_map : hint_normal, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D terrain_roughness_map : filter_linear_mipmap_anisotropic, repeat_enable;
uniform float terrain_detail_chroma = 0.0;

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1,311.7))) * 43758.5453); }
float noise(vec2 p) {
    vec2 i=floor(p), f=fract(p); f=f*f*(3.0-2.0*f);
    return mix(mix(hash(i),hash(i+vec2(1.0,0.0)),f.x),
        mix(hash(i+vec2(0.0,1.0)),hash(i+vec2(1.0,1.0)),f.x),f.y);
}
void vertex() { terrain_position=VERTEX; terrain_normal=NORMAL; }
void fragment() {
    vec2 p=terrain_position.xz;
    vec2 world_seed=vec2(terrain_seed*173.7,terrain_seed*311.9);
    float broad=noise(p*0.012+world_seed)*0.6+noise(p*0.054+world_seed.yx)*0.4;
    float fine=noise(p*0.42+world_seed*2.7);
    float mottled=noise(p*0.14+vec2(17.0,41.0)+world_seed*0.43);
    vec3 grass=mix(terrain_low,terrain_high,broad);
    vec3 dirt=mix(terrain_exposed_low,terrain_exposed_high,broad);
    // Keep paving to the civic apron; access roads are footprint-aware geometry.
    float clearing=1.0-smoothstep(20.0,38.0,length(p));
    float rocky=smoothstep(0.06,0.24,1.0-terrain_normal.y);
    vec3 base=mix(grass,dirt,max(clearing*0.58,rocky));
    float district_center=1.0-smoothstep(12.0,42.0,length(p));
    float district_wear=0.94+mottled*0.06;
    vec2 detail_uv=p/5.0+world_seed*0.013;
    vec3 photographed=texture(terrain_detail,detail_uv).rgb;
    float photographed_luma=dot(photographed,vec3(0.299,0.587,0.114));
    vec3 terrain_micro=mix(vec3(photographed_luma),photographed,terrain_detail_chroma);
    terrain_micro=clamp(terrain_micro/0.32,vec3(0.36),vec3(1.9));
    // Broad cool variation and restrained chroma keep the terrain geological rather
    // than reading as a uniformly painted game board.
    float drainage=noise(p*.006+vec2(81.,13.)+world_seed*.19);
    float basin=noise(p*.0021+world_seed*1.7)*.62+noise(p*.009+vec2(19.,67.)+world_seed)*.38;
    float stratum=abs(noise(p*.018+world_seed.yx)-.5)*2.;
    vec3 cool_shadow=base*vec3(.79,.86,.88);
    vec3 natural=mix(cool_shadow,base,smoothstep(.22,.78,drainage));
    // Basin moisture, exposed strata, and the photographed micro-normal make the
    // large ground read as terrain rather than one uniformly airbrushed colour.
    natural=mix(natural,natural*vec3(.67,.73,.70),smoothstep(.16,.39,basin)*.34);
    natural=mix(natural,natural*vec3(1.16,1.08,.91),smoothstep(.72,.94,stratum)*.16);
    natural*=.70+fine*.14;
    natural*=mix(1.0,district_wear,district_center*.45);
    natural*=mix(vec3(1),terrain_micro,.46);
    // Permanent civic center paving joins the existing district roads. Fine seams
    // resolve near street level and fade into coherent city blocks from above.
    float radius=length(p);
    float town=1.0-smoothstep(30.0,40.0,radius);
    vec2 tile=abs(fract(p/5.0)-.5);
    vec2 aa=max(fwidth(p/5.0),vec2(.004));
    float joint=max(smoothstep(.485-aa.x,.495+aa.x,tile.x),smoothstep(.485-aa.y,.495+aa.y,tile.y));
    float street=0.0;
    vec3 paving=mix(vec3(.095,.112,.119),vec3(.145,.153,.151),mottled)*(1.0-joint*.14);
    paving=mix(paving,vec3(.026,.036,.043),street);
    ALBEDO=mix(natural,paving,town);
    NORMAL_MAP=mix(texture(terrain_normal_map,detail_uv).rgb,vec3(.5,.5,1.0),town);
    NORMAL_MAP_DEPTH=.72;
    ROUGHNESS=mix(clamp(texture(terrain_roughness_map,detail_uv).r*terrain_roughness,.48,1.0),.88,town);
}
