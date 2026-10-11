#version 330 core

#include "common.glsl"

in vec3 vWorld;
in vec3 vNormal;
in vec4 vColor;

layout(location = 0) out vec4  outColor;
layout(location = 1) out float outDistance;

void main()
{
    float dist = length(vWorld - uCamPos);
    outDistance = dist;

    // Thin parts (bulwarks, crane arm) are seen from both sides.
    vec3 N = normalize(vNormal);
    if (!gl_FrontFacing) N = -N;

    vec3 col;
    if (vColor.a > 0.5) {
        col = vColor.rgb * mix(1.2, 6.0, uNight);   // lamps: dim by day, bright at night
    } else {
        col = vColor.rgb * surfaceLight(vWorld, N);
        // A hint of specular so painted surfaces read as painted.
        vec3 V = normalize(uCamPos - vWorld);
        vec3 H = normalize(V + uSunDir);
        col += uSunColor * uSunIntensity * 0.06 * pow(max(dot(N, H), 0.0), 40.0);
    }

    vec3 rd = (vWorld - uCamPos) / max(dist, 1e-4);
    outColor = vec4(mix(col, fogColor(rd), fogAmount(dist)), 1.0);
}
