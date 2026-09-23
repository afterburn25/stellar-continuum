#version 450
// HDR resolve: samples the premultiplied floating-point scene target and
// compresses above-knee luminance into the display band. The SDR range is
// preserved almost exactly; values that would have hard-clamped in UNORM
// roll off asymptotically instead, keeping bright ordering distinguishable.
layout(location=0) in vec2 texture_uv;
layout(set=2,binding=0) uniform sampler2D hdr_source;
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
void main() {
    vec4 texel=texture(hdr_source,texture_uv);
    float alpha=texel.a;
    vec3 unpremultiplied=alpha>0.00001?texel.rgb/alpha:vec3(0.0);
    color=vec4(tonemap(unpremultiplied)*alpha,alpha);
}
