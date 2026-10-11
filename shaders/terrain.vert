#version 330 core

layout(location = 0) in vec3  aPos;       // relative to the chunk corner
layout(location = 1) in vec3  aNormal;
layout(location = 2) in float aMoisture;

uniform mat4 uViewProj;
uniform vec3 uChunkOffset;  // chunk corner relative to the floating origin

out vec3  vWorld;
out vec3  vNormal;
out float vMoisture;

void main()
{
    vWorld    = aPos + uChunkOffset;
    vNormal   = aNormal;
    vMoisture = aMoisture;
    gl_Position = uViewProj * vec4(vWorld, 1.0);
}
