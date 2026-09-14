// Original deterministic, seamless 3D value noise; bounded six-octave evaluation.
uniform float seed = 0.0;
float phash(vec3 p) {
    p=fract(p*vec3(.1031,.1030,.0973)+fract(seed*.0137));
    p+=dot(p,p.yxz+33.33);
    return fract((p.x+p.y)*p.z);
}
float pnoise(vec3 p) {
    vec3 i=floor(p), f=fract(p); f=f*f*(3.0-2.0*f);
    return mix(mix(mix(phash(i),phash(i+vec3(1,0,0)),f.x),mix(phash(i+vec3(0,1,0)),phash(i+vec3(1,1,0)),f.x),f.y),
        mix(mix(phash(i+vec3(0,0,1)),phash(i+vec3(1,0,1)),f.x),mix(phash(i+vec3(0,1,1)),phash(i+vec3(1,1,1)),f.x),f.y),f.z);
}
float pfbm(vec3 p) {
    float v=0.0, w=.51;
    for(int i=0;i<6;i++){v+=pnoise(p)*w;p=p*2.03+vec3(17.1,3.7,9.2);w*=.49;}
    return v;
}
float fracture(vec3 p) {
    vec3 cell=floor(p);float nearest=9.0,second=9.0;
    for(int x=-1;x<=1;x++)for(int y=-1;y<=1;y++)for(int z=-1;z<=1;z++){
        vec3 c=cell+vec3(float(x),float(y),float(z));
        vec3 center=c+vec3(phash(c),phash(c+17.0),phash(c+43.0));
        float d=length(p-center);
        if(d<nearest){second=nearest;nearest=d;}else{second=min(second,d);}
    }
    return 1.0-smoothstep(.009,.038,second-nearest);
}
// Cell craters have independently varied size, ejecta rim, and bowl.
float craters(vec3 p) {
    vec3 cell=floor(p); float h=0.0;
    for(int x=-1;x<=1;x++)for(int y=-1;y<=1;y++)for(int z=-1;z<=1;z++){
        vec3 c=cell+vec3(float(x),float(y),float(z));
        vec3 center=c+vec3(phash(c),phash(c+17.0),phash(c+43.0));
        float size=.13+.27*phash(c+71.0), d=length(p-center)/size;
        h+=exp(-pow((d-1.0)*8.0,2.0))*.20-(1.0-smoothstep(.15,.93,d))*.24;
    }
    return h;
}
