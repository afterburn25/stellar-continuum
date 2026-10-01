#version 450
// Fullscreen triangle from the vertex index: no vertex buffers are bound.
layout(location=0) out vec2 texture_uv;
void main() {
    vec2 p=vec2(float((gl_VertexIndex<<1)&2),float(gl_VertexIndex&2));
    texture_uv=vec2(p.x,1.0-p.y);
    gl_Position=vec4(p*2.0-1.0,0.0,1.0);
}
