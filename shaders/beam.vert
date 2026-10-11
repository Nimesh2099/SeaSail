#version 330 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aTS;     // t: 0 at the lamp .. 1 at the far end; s: -1..1 across

uniform mat4 uModel;
uniform mat4 uViewProj;

out vec3 vWorld;
out vec2 vTS;

void main()
{
    vec4 w = uModel * vec4(aPos, 1.0);
    vWorld = w.xyz;
    vTS = aTS;
    gl_Position = uViewProj * w;
}
