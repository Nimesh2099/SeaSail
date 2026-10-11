#version 330 core

#include "common.glsl"

in vec3 vWorld;
in vec3 vNormal;
in vec4 vColor;

uniform float uPropFadeStart;  // props dissolve between these distances
uniform float uPropFadeEnd;

layout(location = 0) out vec4  outColor;
layout(location = 1) out float outDistance;

void main()
{
    float dist = length(vWorld - uCamPos);

    // Dissolve out at the edge of the prop range with a screen-space dither,
    // instead of popping out all at once.
    float fade = smoothstep(uPropFadeStart, uPropFadeEnd, dist);
    if (hash12(gl_FragCoord.xy) < fade) discard;

    outDistance = dist;
    vec3 N = normalize(vNormal);
    if (!gl_FrontFacing) N = -N;   // leaves are thin: light both sides

    vec3 col = vColor.rgb * surfaceLight(vWorld, N);
    vec3 rd = (vWorld - uCamPos) / max(dist, 1e-4);
    outColor = vec4(mix(col, fogColor(rd), fogAmount(dist)), 1.0);
}
