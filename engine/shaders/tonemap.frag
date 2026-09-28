#version 450
// HDR resolve: samples the premultiplied floating-point scene target and
// compresses above-knee luminance into the display band. The SDR range is
// preserved almost exactly; values that would have hard-clamped in UNORM
// roll off asymptotically instead, keeping bright ordering distinguishable.
layout(location=0) in vec2 texture_uv;
layout(set=2,binding=0) uniform sampler2D hdr_source;
// Per-view post controls; the backend resolves the quality tier before
// filling these, so the shader never branches on quality itself.
layout(set=3,binding=0) uniform PostUniform {
    vec4 a; // exposure, bloom strength, bloom threshold, contrast
    vec4 b; // saturation, sharpen, vignette, unused
} post;
layout(location=0) out vec4 color;
const float KNEE=0.9;
// Asymptote KNEE+HEADROOM sits above the display range: inputs near 1.0 keep
// nearly their SDR values (the pipeline must not visibly shift legacy
// material colors), while super-1.0 highlights roll off smoothly instead of
// clipping. The derivative at the knee is 1, so the curve joins C1-continuous.
const float HEADROOM=0.5;
vec3 tonemap(vec3 c) {
    vec3 over=max(c-vec3(KNEE),vec3(0.0));
    vec3 compressed=vec3(1.0)-exp(-over/vec3(HEADROOM));
    return min(c,vec3(KNEE))+HEADROOM*compressed;
}
vec3 unpremul(vec4 t){return t.a>0.00001?t.rgb/t.a:vec3(0.0);}
void main() {
    vec4 texel=textureLod(hdr_source,texture_uv,0.0);
    float alpha=texel.a;
    vec3 c=unpremul(texel);
    // Unsharp mask on the finest level — detail-preserving contrast.
    if(post.b.y>0.0){
        vec3 neighbours=unpremul(textureLodOffset(hdr_source,texture_uv,0.0,ivec2(1,0)))
            +unpremul(textureLodOffset(hdr_source,texture_uv,0.0,ivec2(-1,0)))
            +unpremul(textureLodOffset(hdr_source,texture_uv,0.0,ivec2(0,1)))
            +unpremul(textureLodOffset(hdr_source,texture_uv,0.0,ivec2(0,-1)));
        c+=post.b.y*(c-neighbours*0.25);
    }
    c*=post.a.x; // exposure
    // Bloom: coarse HDR mips are already blurred, cheap and stable under
    // camera motion — threshold on luminance so dim surfaces never bleed.
    vec3 emit=vec3(0.0);
    if(post.a.y>0.0){
        int last=textureQueryLevels(hdr_source)-1;
        vec3 bloom=vec3(0.0);float taps=0.0;
        for(int lod=2;lod<=4&&lod<=last;++lod){
            vec3 s=textureLod(hdr_source,texture_uv,float(lod)).rgb;
            float lum=dot(s,vec3(0.2126,0.7152,0.0722));
            bloom+=s*clamp(lum-post.a.z,0.0,4.0);taps+=1.0;
        }
        if(taps>0.0)emit=post.a.y*bloom/taps;
    }
    c+=emit;
    // Contrast about mid gray, then saturation — both in linear HDR space.
    if(post.a.w!=1.0)c=0.18+(c-vec3(0.18))*post.a.w;
    if(post.b.x!=1.0){
        float lum=dot(c,vec3(0.2126,0.7152,0.0722));
        c=vec3(lum)+(c-vec3(lum))*post.b.x;
    }
    // Bloom is added light: lifting alpha by its luminance keeps halos visible
    // where the scene was transparent, instead of being killed by premultiply.
    alpha=clamp(alpha+dot(emit,vec3(0.2126,0.7152,0.0722)),0.0,1.0);
    vec3 resolved=tonemap(max(c,vec3(0.0)));
    // Vignette darkens post-tonemap display values so the corner falloff is
    // perceptually uniform instead of compressing through the knee.
    if(post.b.z>0.0){
        vec2 p=texture_uv-vec2(0.5);
        resolved*=1.0-post.b.z*smoothstep(0.2,1.0,dot(p,p)*2.0);
    }
    color=vec4(resolved*alpha,alpha);
}
