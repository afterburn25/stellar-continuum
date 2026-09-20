#version 450
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec2 uv;
layout(set=1,binding=0,std140) uniform Transform {
    mat4 model_view_projection;
    mat4 model_view;
    mat4 shadow_from_model;
};
layout(location=0) out vec3 view_normal;
layout(location=1) out vec2 texture_uv;
layout(location=2) out vec3 view_position;
layout(location=3) out vec3 shadow_position;
void main() {
    gl_Position=model_view_projection*vec4(position,1.0);
    view_normal=mat3(model_view)*normal;
    texture_uv=uv;
    view_position=(model_view*vec4(position,1.0)).xyz;
    shadow_position=(shadow_from_model*vec4(position,1.0)).xyz;
}
