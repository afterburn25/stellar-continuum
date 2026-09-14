shader_type spatial;
render_mode diffuse_burley;
#include "planet_identity_noise.gdshaderinc"
uniform vec3 land_color : source_color = vec3(.3,.4,.25);
uniform vec3 rock_color : source_color = vec3(.4,.35,.3);
uniform vec3 ocean_color : source_color = vec3(.03,.15,.25);
uniform int pattern = 3;
uniform float water = 0.0;
uniform float roughness = .9;
uniform float emission_strength = 0.0;
uniform bool ocean_world = false;
uniform bool preview = false;
uniform sampler2D mineral_detail : filter_linear_mipmap_anisotropic, repeat_enable;
varying vec3 ground;
varying vec3 ground_normal;
void vertex(){ground=VERTEX;ground_normal=NORMAL;}
void fragment(){
    vec3 p=vec3(ground.x,0.0,ground.z);
    float broad=pfbm(p*.012), grit=pnoise(p*6.0), fine=pfbm(p*.7);
    vec3 color=mix(rock_color,land_color,smoothstep(.28,.7,broad));
    float relief=fine, cracks=0.0;
    if(pattern==0){float c=craters(p*.08)+craters(p*.23)*.3;relief+=c;color*=.75+c*1.25;}
    if(pattern==1){float dunes=sin(p.x*.24+p.z*.10+pfbm(p*.015)*12.0);relief+=dunes*.2;color*=.77+dunes*.19;}
    if(pattern==2){cracks=fracture(p*.09+pfbm(p*.02));color=mix(color,rock_color*.25,cracks*.75);relief-=cracks*.4;}
    if(pattern==6){cracks=fracture(p*.12)*smoothstep(.3,.6,broad);color*=.32+fine*.7;relief-=cracks*.4;}
    if(pattern==7){color*=.38+broad*.8;}
    if(pattern==8){float ridge=pow(1.0-abs(pfbm(p*.12)*2.0-1.0),4.0);color=mix(rock_color*.45,land_color*1.2,ridge);relief+=ridge*.4;}
    if(pattern==9){float basin=smoothstep(.32,.37,broad)*(1.0-smoothstep(.50,.54,broad));color=mix(color,land_color*1.65,basin);}
    vec3 micro=texture(mineral_detail,p.xz/22.0).rgb;
    color*=.77+fine*.22+(micro.r-.5)*.16;
    float slope=smoothstep(.08,.35,1.0-ground_normal.y);
    color=mix(color,rock_color*(.45+fine*.55),slope*.65);
    float apron=preview?0.0:1.0-smoothstep(30.0,40.0,length(p));
    vec2 tile=abs(fract(p.xz/5.0)-.5);
    float seam=smoothstep(.475,.497,max(tile.x,tile.y));
    color=mix(color,vec3(.11,.13,.14)*(1.0-seam*.3),apron);
    if(ocean_world){
        float wave=sin(p.x*.13+sin(p.z*.07)*2.0+TIME*.6)*.5+sin(p.z*.23-TIME*.8)*.25;
        color=ocean_color*(.72+wave*.18+fine*.27);
        relief=wave*.1;
        color=mix(color,vec3(.12,.15,.16),apron); // Existing colony is a supported platform.
    }
    ALBEDO=color;
    NORMAL_MAP=normalize(vec3(clamp(-dFdx(relief),-.08,.08),clamp(-dFdy(relief),-.08,.08),1.0))*.5+.5;
    NORMAL_MAP_DEPTH=.18;
    ROUGHNESS=ocean_world?mix(.23,.88,apron):clamp(roughness+(micro.g-.5)*.14,.3,1.0);
    SPECULAR=ocean_world?.65:.3;
    EMISSION=vec3(1.0,.18,.01)*cracks*emission_strength*(1.0-apron)*1.7;
}
