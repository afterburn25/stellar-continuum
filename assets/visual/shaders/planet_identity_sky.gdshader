shader_type sky;
#include "planet_identity_noise.gdshaderinc"
uniform vec3 nebula_color : source_color = vec3(.11,.16,.22);
uniform vec3 dust_color : source_color = vec3(.12,.10,.07);
uniform vec3 atmosphere_color : source_color = vec3(.35,.52,.68);
uniform vec3 star_color : source_color = vec3(1.0,.93,.8);
uniform vec3 secondary_color : source_color = vec3(.65,.78,1.0);
uniform vec3 tertiary_color : source_color = vec3(1.0,.5,.28);
uniform float star_density = .7;
uniform float nebula_visibility = .07;
uniform float dust = .1;
uniform float atmosphere = 0.0;
uniform float cloud_cover = 0.0;
uniform float sun_size = .00465;
uniform float companions = 0.0;
uniform bool surface_view = false;
uniform vec3 sun_direction = vec3(-.46,.53,-.71);
void sky(){
    vec3 d=normalize(EYEDIR);
    float n=pfbm(d*3.5);
    float band=exp(-pow((d.y+.18*sin(d.x*3.0+seed*.01))*3.5,2.0));
    float filament=pfbm(d*37.0+n*3.0);
    vec3 space=vec3(.0005)+nebula_color*smoothstep(.31,.72,n)*band*nebula_visibility;
    space+=dust_color*band*filament*dust*.11;
    space*=1.0-smoothstep(.48,.70,n)*dust*.7;
    vec3 cell=floor(d*720.0), f=fract(d*720.0)-.5;
    float stars=pow(max(0.0,1.0-length(f)*1.8),7.0)*step(1.0-.01*star_density,phash(cell));
    space+=mix(vec3(.61,.76,1.0),vec3(1.0,.85,.64),phash(cell+17.0))*stars*1.6;
    if(surface_view){
        float horizon=pow(1.0-max(d.y,0.0),3.0);
        vec3 scatter=mix(atmosphere_color*.37,atmosphere_color,horizon*.78);
        scatter*=mix(vec3(1.0),star_color,.45);
        float clouds=smoothstep(.49,.72,pfbm(d*vec3(8.0,24.0,8.0)));
        scatter=mix(scatter,atmosphere_color*.92,clouds*cloud_cover);
        space=mix(space,scatter,clamp(atmosphere*(.55+horizon*.45),0.0,.985));
        float sd=distance(d,normalize(sun_direction));
        float disc=1.0-smoothstep(sun_size*.85,sun_size,sd);
        float glow=exp(-sd/(sun_size*4.0))*.27*atmosphere;
        space+=star_color*(disc*4.0+glow)*(1.0-clouds*cloud_cover*.87);
        if(companions>0.5){
            float bd=distance(d,normalize(vec3(-.16,.46,-.87)));
            space+=secondary_color*(1.0-smoothstep(sun_size*.62,sun_size*.75,bd))*2.7*(1.0-cloud_cover*.6);
        }
        if(companions>1.5){
            float cd=distance(d,normalize(vec3(.3,.32,-.9)));
            space+=tertiary_color*(1.0-smoothstep(sun_size*.42,sun_size*.55,cd))*2.2*(1.0-cloud_cover*.6);
        }
    }
    COLOR=space;
}
