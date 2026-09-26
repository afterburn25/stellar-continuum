#version 450
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec2 uv;
// Light-space clip transforms, indexed by gl_InstanceIndex like the main
// pass. Each entry also carries the caster's signed screen-door keep
// probability so LOD/range transitions fade the shadow silhouette in
// step with the lit pass instead of popping, plus the material's alpha
// cutout threshold and UV tiling so the depth pass can discard the same
// texels the lit pass drops.
struct ShadowCast {
    mat4 light_mvp;
    float keep;
    float alpha_threshold;
    vec2 tile;
};
layout(set=0,binding=0,std430) readonly buffer Transforms {
    ShadowCast casts[];
};
layout(location=0) flat out float keep;
layout(location=1) flat out vec3 cutout;
layout(location=2) out vec2 tex_uv;
void main() {
    gl_Position=casts[gl_InstanceIndex].light_mvp*vec4(position,1.0);
    keep=casts[gl_InstanceIndex].keep;
    cutout=vec3(casts[gl_InstanceIndex].alpha_threshold,casts[gl_InstanceIndex].tile);
    tex_uv=uv;
}
