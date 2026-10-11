#version 330 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec4 aColor;   // rgb + emissive flag

uniform mat4 uModel;     // object -> camera-relative world
uniform mat4 uViewProj;

out vec3 vWorld;
out vec3 vNormal;
out vec4 vColor;

void main()
{
    vec4 w = uModel * vec4(aPos, 1.0);
    vWorld  = w.xyz;
    vNormal = mat3(uModel) * aNormal;   // rigid transform: no inverse-transpose needed
    vColor  = aColor;
    gl_Position = uViewProj * w;
}
