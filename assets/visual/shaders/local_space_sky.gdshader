shader_type sky;
uniform float seed = 0.0;
float hash(vec3 p) { return fract(sin(dot(p,vec3(127.1,311.7,74.7)) + seed*51.7)*43758.5453); }
float noise(vec3 p) {
    vec3 i=floor(p), f=fract(p); f=f*f*(3.-2.*f);
    return mix(mix(mix(hash(i),hash(i+vec3(1,0,0)),f.x),mix(hash(i+vec3(0,1,0)),hash(i+vec3(1,1,0)),f.x),f.y),
        mix(mix(hash(i+vec3(0,0,1)),hash(i+vec3(1,0,1)),f.x),mix(hash(i+vec3(0,1,1)),hash(i+vec3(1,1,1)),f.x),f.y),f.z);
}
void sky() {
    vec3 d=normalize(EYEDIR);
    float cloud=noise(d*3.)*.5+noise(d*8.)*.25+noise(d*21.)*.15+noise(d*63.)*.1;
    float filaments=noise(d*48.+cloud*2.);
    float band=exp(-pow((d.y+.2*sin(d.x*4.+seed)) * 2.5,2.));
    float dust=smoothstep(.30,.72,cloud)*band;
    vec3 cool=mix(vec3(.019,.03,.046),vec3(.061,.046,.028),fract(seed*.618+.15));
    vec3 nebula=cool*dust*(.55+filaments*.45);
    vec3 grid=d*900.; vec3 cell=floor(grid); vec3 f=fract(grid)-.5;
    float point=pow(max(0.,1.-length(f)*1.8),5.)*step(.992,hash(cell));
    float bright=pow(max(0.,1.-length(fract(d*270.)-.5)*1.8),7.)*step(.997,hash(floor(d*270.)));
    vec3 star_color=mix(vec3(.65,.79,1.),vec3(1.,.87,.66),hash(cell+vec3(12,3,7)));
    COLOR=vec3(.001)+nebula+star_color*(point*1.2+bright*2.);
}
