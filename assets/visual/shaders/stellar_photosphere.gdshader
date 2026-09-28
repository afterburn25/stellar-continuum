shader_type spatial;
render_mode unshaded;
uniform vec4 star_color : source_color = vec4(1,.65,.2,1);
uniform float seed = 1.;
uniform float solar_treatment : hint_range(0.,1.) = 0.;
varying vec3 sphere_point;
float hash(vec3 p){ return fract(sin(dot(p,vec3(127.1,311.7,74.7)))*43758.5453); }
float noise(vec3 p){
    vec3 i=floor(p),f=fract(p); f=f*f*(3.-2.*f);
    return mix(mix(mix(hash(i),hash(i+vec3(1,0,0)),f.x),mix(hash(i+vec3(0,1,0)),hash(i+vec3(1,1,0)),f.x),f.y),
        mix(mix(hash(i+vec3(0,0,1)),hash(i+vec3(1,0,1)),f.x),mix(hash(i+vec3(0,1,1)),hash(i+vec3(1,1,1)),f.x),f.y),f.z);
}
void vertex(){sphere_point=normalize(VERTEX);}
void fragment(){
    vec3 drift=vec3(TIME*.012,-TIME*.008,TIME*.005);
    float broad=noise(sphere_point*7.3+vec3(seed*.071)+drift*.08);
    vec3 warped=sphere_point*(31.+broad*8.)+drift+vec3(seed*.031);
    float granule_a=noise(warped);
    float granule_b=noise(sphere_point*91.-drift*.63+granule_a*2.7);
    float granule_c=noise(sphere_point*183.+drift*.37+granule_b*1.9);
    float granules=granule_a*.40+granule_b*.37+granule_c*.23;
    // Fine broken intergranular lanes retain boiling motion without reading as craters.
    float lanes=1.-smoothstep(.026,.11,abs(granule_b-.50+(.5-granule_c)*.13));
    float convection=noise(sphere_point*13.7+vec3(seed*.113)-drift*.11);
    // Low-frequency warped fields form sparse magnetic groups. A slowly changing threshold
    // makes their ragged penumbrae grow and erode without a repeating dot lattice.
    float spot_warp=noise(sphere_point*3.1+vec3(seed*.137)+drift*.025);
    float spot_field=noise(sphere_point*5.3+vec3(seed*.053,-seed*.091,TIME*.0022)+spot_warp*2.4);
    float spot_detail=noise(sphere_point*15.9+vec3(seed*.019)-drift*.07);
    float spot_birth=.035*sin(TIME*.017+seed*1.7);
    float penumbra=smoothstep(.83+spot_birth,.94+spot_birth,spot_field)*smoothstep(.53,.78,spot_detail);
    float umbra=smoothstep(.91+spot_birth,.985+spot_birth,spot_field)*smoothstep(.69,.88,spot_detail);
    float view_facing=max(dot(normalize(NORMAL),normalize(VIEW)),0.);
    float rim=pow(1.-view_facing,2.15);
    float limb=.78+.22*pow(view_facing,.30);
    // Keep a broad orange-to-yellow range even when the spectral tint is pale. Bright
    // intergranular walls surround deeper amber plasma cells, like the supplied solar view.
    vec3 spectral_deep=star_color.rgb*vec3(.70,.61,.54);
    vec3 spectral_mid=mix(star_color.rgb,vec3(1.),.10);
    vec3 spectral_high=mix(star_color.rgb,vec3(1.),.34);
    vec3 deep_orange=mix(spectral_deep,vec3(1.,.20,.012),solar_treatment);
    vec3 molten_gold=mix(spectral_mid,vec3(1.,.53,.035),solar_treatment);
    vec3 solar_yellow=mix(spectral_high,vec3(1.,.87,.28),solar_treatment);
    vec3 rim_white=mix(spectral_high,vec3(1.,.985,.72),solar_treatment);
    float cell_light=smoothstep(.35,.74,granules*.76+convection*.24);
    vec3 plasma=mix(deep_orange,molten_gold,cell_light);
    plasma=mix(plasma,solar_yellow,smoothstep(.62,.91,granules)*.72);
    float magnetic_darkening=1.-penumbra*.33-umbra*.43;
    float boiling=.67+granules*.30+convection*.085;
    float luminous_network=lanes*(.22+.16*granule_c);
    ALBEDO=plasma*boiling*limb*magnetic_darkening
        +solar_yellow*luminous_network+rim_white*pow(rim,3.4)*.72;
    // Uneven emission retains the turbulent orange structure instead of bleaching the disk.
    float radiance=.68+cell_light*.54+luminous_network*.72+pow(rim,3.8)*.90;
    EMISSION=ALBEDO*radiance;
}
