shader_type spatial;
render_mode unshaded;
#include "planet_identity_noise.gdshaderinc"
uniform vec3 land_color : source_color = vec3(.3,.4,.25);
uniform vec3 rock_color : source_color = vec3(.4,.35,.3);
uniform vec3 ocean_color : source_color = vec3(.03,.15,.25);
uniform vec3 atmosphere_color : source_color = vec3(.4,.6,.8);
uniform vec3 star_color : source_color = vec3(1.0,.95,.85);
uniform vec3 secondary_color : source_color = vec3(.6,.8,1.0);
uniform float secondary_strength = 0.0;
uniform float variant_index = 0.0;
uniform int pattern = 3;
uniform float water = .65;
uniform float clouds = .45;
uniform float haze = .15;
uniform float ice = 0.0;
uniform float emission_strength = 0.0;
uniform float development = 0.0;
uniform bool giant = false;
uniform bool approved_map_available = false;
uniform sampler2D approved_map : source_color, filter_linear_mipmap_anisotropic, repeat_enable;
uniform vec3 sun_direction = vec3(-.7,.35,.6);
varying vec3 unit_position;
void vertex(){unit_position=normalize(VERTEX);}
void fragment(){
    vec3 p=normalize(unit_position);
    float scale=3.1+variant_index*.47;
    float broad=pfbm(p*scale), detail=pfbm(p*72.0);
    float elevation=broad*.83+detail*.17;
    float wet=1.0-smoothstep(water*.45+.14,water*.45+.16,elevation);
    if(water<.01)wet=0.0;
    if(water>=.90)wet=1.0;
    else if(water>.84)wet=1.0-smoothstep(.70,.75,elevation);
    vec3 base=mix(rock_color,land_color,smoothstep(.27,.67,elevation));
    float relief=detail;
    if(pattern==0){float c=craters(p*16.0)+craters(p*43.0)*.32; base*=.82+c*1.7+detail*.34;relief+=c;}
    if(pattern==1){float dunes=sin((p.x*32.0+p.z*16.0)+pfbm(p*5.0)*20.0);base*=.76+.21*dunes+detail*.40;relief+=dunes*.14;}
    if(pattern==2){float cracks=fracture(p*19.0+pfbm(p*8.0)*1.5);base=mix(base,rock_color*.42,cracks*.65);relief-=cracks*.3;}
    if(pattern==7){base*=.42+pfbm(p*23.0)*.78;}
    if(pattern==8){float ridge=1.0-abs(pfbm(p*13.0)*2.0-1.0);base=mix(rock_color*.5,base*1.25,pow(ridge,3.0));relief+=ridge;}
    if(pattern==9){float basin=smoothstep(.34,.39,elevation)*(1.0-smoothstep(.46,.52,elevation));base=mix(base,land_color*1.6,basin*.8);}
    base=mix(base,ocean_color*(.64+detail*.55),wet);
    float polar=smoothstep(.79-ice*.15,.97-ice*.09,abs(p.y)+pfbm(p*9.0)*.12);
    base=mix(base,vec3(.78,.85,.85),polar*ice);
    float storm_noise=pfbm(p*8.0+vec3(sin(p.y*14.0+variant_index),0,cos(p.y*14.0))* .7);
    float cloud_mask=smoothstep(1.0-clouds*.78,1.13-clouds*.78,storm_noise);
    if(giant||pattern==5) {
        float band=sin(p.y*(34.0+variant_index*8.0)+storm_noise*(14.0+variant_index*2.0));
        float eddy=sin(atan(p.z,p.x)*9.0+storm_noise*22.0)*sin(p.y*19.0+storm_noise*9.0);
        base=mix(rock_color,land_color,clamp(.5+band*.32+eddy*.14,0.0,1.0));
        base=mix(base,ocean_color,smoothstep(.7,.94,storm_noise)*.55);
        base*=.83+detail*.33;wet=0.0;
    }else base=mix(base,mix(vec3(.88,.89,.87),atmosphere_color,.20),cloud_mask);
    if(approved_map_available)base=texture(approved_map,vec2(atan(p.z,p.x)/6.2831853+.5,acos(clamp(p.y,-1.0,1.0))/3.14159265)).rgb;
    float ndl=dot(p,normalize(sun_direction));
    float light=smoothstep(-.13,.7,ndl);
    vec3 light_color=mix(vec3(.017,.023,.034),star_color,light);
    float secondary=max(0.0,dot(p,normalize(vec3(.65,.38,-.4))))*secondary_strength;
    vec3 color=base*(light_color+secondary_color*secondary);
    color*=.95+detail*.10;
    float spec=pow(max(0.0,dot(reflect(-normalize(sun_direction),p),normalize(vec3(0,0,1)))),65.0);
    color+=star_color*spec*wet*.35;
    float rim=pow(1.0-max(0.0,dot(NORMAL,VIEW)),3.4);
    color+=atmosphere_color*rim*haze*.29*smoothstep(-.32,.5,ndl);
    if(emission_strength>0.0){float lava=fracture(p*26.0)*smoothstep(.42,.58,pfbm(p*8.0));color+=vec3(1.0,.18,.012)*lava*emission_strength*1.5;}
    float cities=step(.982-development*.016,phash(floor(p*1100.0)))*smoothstep(.4,.58,elevation)*(1.0-wet);
    color+=vec3(1.0,.66,.24)*cities*development*(1.0-smoothstep(-.12,.15,ndl))*2.5;
    ALBEDO=color;
}
