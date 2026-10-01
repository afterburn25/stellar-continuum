#version 450
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec2 uv;
struct Transform {
    mat4 model_view_projection;
    mat4 model_view;
    mat4 shadow_from_model;
};
layout(set=0,binding=0,std430) readonly buffer Transforms {
    Transform transforms[];
};
layout(location=0) out vec3 view_normal;
layout(location=1) out vec2 texture_uv;
layout(location=2) out vec3 view_position;
layout(location=3) out vec3 shadow_position;
// The instance index travels flat so the fragment stage reads the same
// material record — Vulkan's gl_InstanceIndex includes first_instance.
layout(location=4) flat out uint instance_index;
void main() {
    instance_index=gl_InstanceIndex;
    Transform transform=transforms[gl_InstanceIndex];
    gl_Position=transform.model_view_projection*vec4(position,1.0);
    view_normal=mat3(transform.model_view)*normal;
    texture_uv=uv;
    view_position=(transform.model_view*vec4(position,1.0)).xyz;
    shadow_position=(transform.shadow_from_model*vec4(position,1.0)).xyz;
}
