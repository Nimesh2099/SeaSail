#version 330 core

// Full-screen triangle generated from gl_VertexID (no vertex buffer).
// Vertices (-1,-1), (3,-1), (-1,3) cover the whole screen.
out vec2 vNdc;

void main()
{
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2)) * 2.0 - 1.0;
    vNdc = p;
    gl_Position = vec4(p, 1.0, 1.0);
}
