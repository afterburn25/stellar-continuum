shader_type spatial;
render_mode unshaded;
uniform sampler2D surface_map : source_color, filter_linear_mipmap_anisotropic;
uniform vec4 base_color : source_color = vec4(.35,.3,.25,1);
uniform bool mapped = false;
uniform bool known = true;
uniform bool has_oceans = false;
uniform bool gas_giant = false;
uniform bool saturn = false;
uniform bool city_lights = false;
uniform bool photographic_earth = false;
uniform sampler2D earth_photo : source_color, filter_linear_mipmap_anisotropic;
uniform vec4 earth_photo_disc = vec4(0.5280, 0.5836, 0.1851, 0.1968);
uniform sampler2D night_map : source_color, filter_linear_mipmap_anisotropic;
// Supplied in the globe's local frame, so orbit and rebased descent agree.
uniform vec3 sun_direction = vec3(0.,1.,0.);
uniform float seed = 0.;
varying vec3 globe_point;
float hash(vec3 p){return fract(sin(dot(p,vec3(127.1,311.7,74.7))+seed)*43758.5453);}
float noise(vec3 p){
    vec3 i=floor(p),f=fract(p); f=f*f*(3.-2.*f);
    return mix(mix(mix(hash(i),hash(i+vec3(1,0,0)),f.x),mix(hash(i+vec3(0,1,0)),hash(i+vec3(1,1,0)),f.x),f.y),
        mix(mix(hash(i+vec3(0,0,1)),hash(i+vec3(1,0,1)),f.x),mix(hash(i+vec3(0,1,1)),hash(i+vec3(1,1,1)),f.x),f.y),f.z);
}
void vertex(){globe_point=normalize(VERTEX);}
void fragment(){
    vec3 p=normalize(globe_point);
    float continental=noise(p*3.4)*.48+noise(p*9.)*.28+noise(p*28.)*.16+noise(p*95.)*.08;
    float terrain=noise(p*9.)*.5+noise(p*28.)*.28+noise(p*95.)*.15+noise(p*260.)*.07;
    vec3 color=base_color.rgb*(.54+continental*.54+terrain*.18);
    // Fine relief and low-frequency mineral variation make unmapped worlds retain
    // a readable terrain hierarchy at close focus without invented image textures.
    float escarpment=abs(noise(p*44.)-.5)*2.;
    color=mix(color,color*vec3(.69,.73,.75),smoothstep(.82,.98,escarpment)*.22);
    if(gas_giant){
        float bands=sin(p.y*110.+noise(p*17.)*5.)*.5+.5;
        color=base_color.rgb*mix(.56,1.3,bands*.5+noise(p*45.)*.5);
    }
    if(mapped)color=texture(surface_map,UV).rgb;
    if(saturn){
        float lightness=dot(color,vec3(.299,.587,.114));
        color=mix(vec3(lightness)*vec3(1.12,.98,.76),color,.20);
        float fine_bands=sin(p.y*220.+noise(p*35.)*.45)*.5+.5;
        color*=.97+fine_bands*.045;
    }
    if(!known)color=base_color.rgb;
    float ocean=has_oceans ? smoothstep(.02,.09,color.b-max(color.r,color.g*.9)) : 0.;
    // Astronomical model distances are compressed for navigation. Shade from the
    // local stellar direction rather than distance-attenuating the whole globe to black.
    float cosine=dot(p,normalize(sun_direction));
    float diffuse=max(cosine,0.);
    float day=smoothstep(-.09,.12,cosine);
    ALBEDO=color*(.04+pow(diffuse,.68)*1.20*day);
    ROUGHNESS=mix(.88,.42,ocean);
    SPECULAR=mix(.28,.38,ocean);
    METALLIC=0.;
    if(photographic_earth){
        // Keep the old photograph's observed hemisphere rather than wrapping it into
        // invented continents. Native globe geometry still provides depth/occlusion.
        vec3 facing_normal=normalize(NORMAL);
        vec2 photo_uv=earth_photo_disc.xy+vec2(facing_normal.x,-facing_normal.y)*earth_photo_disc.zw;
        // This approved disc photograph already contains its observed lighting.
        // A second artificial terminator would darken the original twice.
        ALBEDO=texture(earth_photo,photo_uv).rgb;
    }
    if(city_lights){
        vec3 city=texture(night_map,UV).rgb;
        float night=1.-smoothstep(-.16,.10,dot(p,normalize(sun_direction)));
        // Black Marble includes a faint land albedo. Reject it so only real city
        // clusters emit; the daytime hemisphere stays under the physical light.
        float luminance=max(city.r,max(city.g,city.b));
        EMISSION=city*smoothstep(.10,.36,luminance)*night*1.6;
    }
}
