shader_type canvas_item;
render_mode unshaded;

// Distant gas and dust only: no galaxy images or artificial celestial objects.
uniform float scenery_seed = 0.0;
uniform float aspect = 1.77778;
uniform vec4 gas_color : source_color = vec4(.22,.12,.05,1);
uniform vec4 secondary_color : source_color = vec4(.06,.10,.17,1);
uniform float gas_density = .6;
float hash(vec2 p){ return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453); }
float noise(vec2 p){
    vec2 i=floor(p), f=fract(p); f=f*f*(3.0-2.0*f);
    return mix(mix(hash(i),hash(i+vec2(1,0)),f.x),mix(hash(i+vec2(0,1)),hash(i+vec2(1,1)),f.x),f.y);
}
float fbm(vec2 p){
    float sum=0.0, a=.5;
    mat2 r=mat2(vec2(.8,.6),vec2(-.6,.8));
    for(int i=0;i<6;i++){sum+=noise(p)*a;p=r*p*2.07+vec2(2.7,6.3);a*=.5;}
    return sum;
}
void fragment(){
    float angle=scenery_seed*.71;
    vec2 p=(UV-.5)*vec2(aspect,1.0)*3.2;
    p=mat2(vec2(cos(angle),sin(angle)),vec2(-sin(angle),cos(angle)))*p;
    p+=vec2(scenery_seed*13.71,scenery_seed*7.19);
    vec2 flow=vec2(fbm(p*.6),fbm(p*.6+vec2(19.1,3.7)));
    float cloud=fbm(p+flow*2.9);
    float lanes=fbm(p*2.7+flow*4.0);
    float body=smoothstep(.26,.79,cloud);
    float dust=smoothstep(.35,.68,lanes);
    vec3 gas=mix(gas_color.rgb,secondary_color.rgb,smoothstep(.36,.66,flow.x));
    vec3 color=vec3(.0015,.0025,.0045)+gas*body*(.18+.82*dust)*gas_density;
    COLOR=vec4(color,1);
}
