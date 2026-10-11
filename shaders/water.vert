#version 330 core

// Gerstner waves (Tessendorf; GPU Gems 1, ch. 1). Unlike a plain sine
// wave, each Gerstner wave also moves the surface *sideways* toward its
// crests, so crests are sharp and troughs are wide, like real swell.
//
//   P.xz = x + sum( Q * A * D * cos(theta) )
//   P.y  =     sum( A * sin(theta) ),     theta = k * dot(D, x) + phase

layout(location = 0) in vec2 aPos;   // offset from the camera, metres

const int MAX_WAVES = 10;
uniform vec4 uWaveA[MAX_WAVES];      // dir.x, dir.z, k, amplitude
uniform vec4 uWaveB[MAX_WAVES];      // Q, phase, fade distance, displaces?
uniform int  uWaveCount;

uniform vec2 uCamXZ;
uniform mat4 uViewProj;

out vec3  vWorld;
out vec2  vBase;
out float vCrest;

void main()
{
    vec2  base = aPos + uCamXZ;       // the water mesh follows the camera...
    float dist = length(aPos);
    vec3  p = vec3(base.x, 0.0, base.y);

    for (int i = 0; i < uWaveCount; ++i) {
        if (uWaveB[i].w < 0.5) continue;   // ripple: lighting only
        vec2  D = uWaveA[i].xy;
        float k = uWaveA[i].z;
        // ...but waves are evaluated at the world position, so they stay put.
        float A = uWaveA[i].w * (1.0 - smoothstep(0.5 * uWaveB[i].z, uWaveB[i].z, dist));
        float theta = k * dot(D, base) + uWaveB[i].y;
        p.xz += uWaveB[i].x * A * D * cos(theta);
        p.y  += A * sin(theta);
    }

    vWorld = p;
    vBase  = base;
    vCrest = p.y;
    gl_Position = uViewProj * vec4(p, 1.0);
}
