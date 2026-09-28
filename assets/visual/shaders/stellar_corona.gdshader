shader_type spatial;
render_mode unshaded, blend_add, depth_draw_never, cull_disabled, fog_disabled;
uniform vec4 star_color : source_color = vec4(1.,.7,.3,1.);
uniform float seed = 1.;
uniform float solar_treatment : hint_range(0.,1.) = 0.;
float hash(vec2 p){return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453);}
float noise(vec2 p){vec2 i=floor(p),f=fract(p);f=f*f*(3.-2.*f);return mix(mix(hash(i),hash(i+vec2(1,0)),f.x),mix(hash(i+vec2(0,1)),hash(i+vec2(1,1)),f.x),f.y);}

void vertex(){
    // Camera-facing geometry with a world-scale footprint, never a stretched image.
    MODELVIEW_MATRIX=VIEW_MATRIX*mat4(INV_VIEW_MATRIX[0],INV_VIEW_MATRIX[1],INV_VIEW_MATRIX[2],MODEL_MATRIX[3]);
}
void fragment(){
    vec2 q=(UV-.5)*2.;
    float radius=length(q);
    float angle=atan(q.y,q.x);
    float turbulence=noise(vec2(angle*3.2,TIME*.018)+q*4.);
    float rays=.68+.15*sin(angle*13.+TIME*.045)+.09*sin(angle*29.-TIME*.019)+.16*turbulence;
    float halo=exp(-max(0.,radius-.425)*7.2)*.42;
    float outer_aura=exp(-max(0.,radius-.43)*3.15)*.14;
    float streamers=pow(max(0.,1.-radius),2.5)*rays*.28;
    float edge=1.-smoothstep(.83,1.,radius);
    // The photosphere is a separate depth-tested sphere. Never paint radial corona
    // streamers through the billboard interior when a close camera views it off-axis.
    float outside_limb=smoothstep(.405,.455,radius);
    // One seeded eruption opportunity per long slot leaves genuinely quiet intervals.
    // The envelope is already zero at both slot boundaries, so a new random event cannot flicker in.
    float slot_length=9.5+hash(vec2(seed,4.7))*3.5;
    float shifted_time=TIME+hash(vec2(seed,17.1))*slot_length;
    float epoch=floor(shifted_time/slot_length);
    float slot_phase=fract(shifted_time/slot_length);
    float event_roll=hash(vec2(seed*2.31+epoch,31.7));
    float previous_roll=hash(vec2(seed*2.31+epoch-1.,31.7));
    // Random slots may be skipped, but never two in a row; a real-time visual check
    // therefore observes an eruption within a bounded two-slot window.
    float eruption_enabled=max(step(.48,event_roll),1.-step(.48,previous_roll));
    float start=.08+hash(vec2(seed+epoch*1.17,8.3))*.12;
    float duration=.29+hash(vec2(seed*3.07,epoch+12.4))*.20;
    float life=clamp((slot_phase-start)/duration,0.,1.);
    float growth=smoothstep(0.,.28,life);
    float fade=1.-smoothstep(.58,1.,life);
    float envelope=growth*fade*eruption_enabled;

    float bearing=(hash(vec2(epoch*3.17+seed,19.3))-.5)*6.2831853;
    bearing+=(hash(vec2(seed,epoch+2.9))-.5)*.13*sin(life*3.1415926);
    float half_width=.12+.12*hash(vec2(epoch+4.2,seed));
    float height=.10+.17*hash(vec2(seed*1.9,epoch+7.6));
    float size=.72+.45*hash(vec2(epoch+13.8,seed*.71));
    float signed_da=atan(sin(angle-bearing),cos(angle-bearing));
    float da=abs(signed_da);
    float along=clamp(da/half_width,0.,1.);
    // One arch rises highest at its center and returns to the surface at both feet.
    // The previous sin(abs(angle)) profile had a trough at center and formed two M-like loops.
    float arch=sqrt(max(0.,1.-along*along));
    float loop_radius=.435+arch*height*growth;
    float surface_taper=1.-smoothstep(half_width*.70,half_width,da);
    // Signed angular sampling prevents the two sides from mirroring into a geometric heart.
    float ragged_noise=noise(vec2(signed_da*24.+epoch,TIME*.075+seed+radius*3.7));
    float displaced=loop_radius+(ragged_noise-.5)*(.032+.018*growth)
        +sin(signed_da*39.+epoch)*.008*growth;
    // A broad translucent sheath and uneven hot core make the prominence read as plasma,
    // avoiding the single thin geometric loop produced by the earlier distance line.
    float ragged_alpha=.58+.42*noise(vec2(signed_da*41.-epoch,radius*17.-TIME*.11));
    float sheath=exp(-abs(radius-displaced)*(19./size))*surface_taper*ragged_alpha;
    float core=exp(-abs(radius-displaced)*(39./size))*surface_taper
        *smoothstep(.22,.58,ragged_noise+.18);
    float plume=(sheath*.34+core*.68)*(1.-along*.24);
    // A small diffuse leading spray lifts away after the loop opens. It remains secondary
    // to the thick tapered prominence rather than becoming a large oval endpoint.
    float knot_radius=.45+height*(.24+life*1.12);
    float spray=exp(-pow((radius-knot_radius)/(.018+.008*size),2.))
        *exp(-pow(da/(half_width*.20),2.))*smoothstep(.20,.38,life)*fade*.38;
    float flares=(plume+spray)*envelope;
    vec3 spectral_corona=mix(star_color.rgb,vec3(1.),.08);
    vec3 spectral_flare=mix(star_color.rgb,vec3(1.),.38);
    vec3 corona_orange=mix(spectral_corona,vec3(1.,.24,.018),solar_treatment*.64);
    vec3 flare_gold=mix(spectral_flare,vec3(1.,.64,.10),solar_treatment);
    ALBEDO=corona_orange;
    EMISSION=corona_orange*(.52+halo*.85+outer_aura*.35)+flare_gold*flares*6.2;
    ALPHA=(halo+outer_aura+streamers+flares*.82)*edge*outside_limb;
}
