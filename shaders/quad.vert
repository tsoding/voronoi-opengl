// Single triangle strip quad generated entirely on the vertex shader.
// Simply do glDrawArrays(GL_TRIANGLE_STRIP, 0, 4) and the shader
// generates 4 points from gl_VertexID. No Vertex Attributes are
// required.
#version 330

precision mediump float;

layout(location = 0) in vec2 seed_pos;
layout(location = 1) in vec4 seed_color;

out vec2 seed;
out vec4 color;

void main(void)
{
    vec2 uv;
    uv.x = (gl_VertexID & 1);
    uv.y = ((gl_VertexID >> 1) & 1);
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
    seed  = seed_pos;
    color = seed_color;
}
