shader_type spatial;
render_mode diffuse_burley;
uniform vec4 wall_color : source_color = vec4(.24,.29,.32,1);
uniform vec4 glass_color : source_color = vec4(.045,.11,.16,1);
uniform float building_seed=1.0;
uniform float window_light=.11;
varying vec3 facade_position;
varying vec3 facade_normal;
float hash(vec2 p){ return fract(sin(dot(p,vec2(127.1,311.7))+building_seed)*43758.5453); }
void vertex(){ facade_position=(MODEL_MATRIX*vec4(VERTEX,1.0)).xyz; facade_normal=normalize(mat3(MODEL_MATRIX)*NORMAL); }
void fragment(){
    vec2 p=vec2(abs(facade_normal.x)>.5?facade_position.z:facade_position.x,facade_position.y);
    vec2 grid=p/vec2(1.55,3.35), cell=floor(grid), f=fract(grid);
    vec2 aa=max(fwidth(grid),vec2(.004));
    float pane=smoothstep(.14-aa.x,.14+aa.x,f.x)*(1.0-smoothstep(.86-aa.x,.86+aa.x,f.x))*
        smoothstep(.15-aa.y,.15+aa.y,f.y)*(1.0-smoothstep(.88-aa.y,.88+aa.y,f.y));
    pane*=1.0-smoothstep(.5,.8,abs(facade_normal.y));
    float variation=hash(cell)*.18+.82;
    // Keep the glazing materially dark in daylight, but make occupied floors legible
    // from the colony overview. A few cool panes retain the instrumentation language;
    // most lit rooms are deliberately warm human-scale occupancy cues.
    vec3 glass=glass_color.rgb*(variation*.70+.12);
    float sky_reflection=pow(clamp(dot(normalize(facade_normal),normalize(vec3(-.35,.78,.42))),0.,1.),3.0);
    glass+=vec3(.08,.18,.23)*sky_reflection*.20;
    float edge_distance=min(min(f.x,1.0-f.x),min(f.y,1.0-f.y));
    float mullion=1.0-smoothstep(.018,.055,edge_distance);
    float floor_band=1.0-smoothstep(.055,.09,min(f.y,1.0-f.y));
    float panel_noise=hash(floor(p*1.7)+vec2(building_seed));
    vec3 cladding=wall_color.rgb*(.74+panel_noise*.13);
    cladding*=mix(.92,1.04,smoothstep(-.1,.75,facade_normal.y));
    // Pane centres are recessed and darker than their framed edges. The layered
    // highlights give the procedural facade a believable depth at street scale
    // without allocating per-window geometry or a texture per building.
    float inset=smoothstep(.10,.22,edge_distance)*(1.0-smoothstep(.38,.48,edge_distance));
    vec3 recessed_glass=glass*(.54+.22*inset);
    vec3 sill=vec3(.045,.055,.058)*(mullion*.72+floor_band*.38);
    float reflected_band=.5+.5*sin((facade_position.y+facade_position.x*.17)*.21+building_seed);
    vec3 reflection=vec3(.16,.30,.39)*(sky_reflection*.22+.035)*(.72+.28*reflected_band);
    ALBEDO=mix(cladding,recessed_glass+reflection,pane)+sill;
    ROUGHNESS=mix(.72+.10*panel_noise,.10+.10*variation+(1.0-inset)*.13,pane);
    METALLIC=mix(.58,.28,pane); SPECULAR=mix(.32,.78,pane);
    float occupied=step(.52,hash(cell+vec2(8,19)));
    vec3 interior=mix(vec3(.30,.64,.78),vec3(1.0,.57,.22),step(.34,hash(cell+vec2(31,7))));
    EMISSION=interior*pane*occupied*window_light;
}
