#version 450
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec2 uv;
// Light-space clip transforms, indexed by gl_InstanceIndex like the main
// pass. Depth-only: the fragment stage is absent and the rasterizer writes
// the shadow map directly.
layout(set=0,binding=0,std430) readonly buffer Transforms {
    mat4 light_mvps[];
};
void main() {
    gl_Position=light_mvps[gl_InstanceIndex]*vec4(position,1.0);
}
