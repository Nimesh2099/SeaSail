#version 330 core

#include "common.glsl"

in vec3  vWorld;
in vec3  vNormal;
in float vMoisture;

uniform int   uWireframe;

layout(location = 0) out vec4  outColor;
layout(location = 1) out float outDistance;

// Light loses red first, then green, as it travels through sea water.
const vec3 kWaterAbsorb = vec3(0.42, 0.065, 0.04);

void main()
{
    float dist = length(vWorld - uCamPos);
    outDistance = dist;
    if (uWireframe == 1) {
        outColor = vec4(0.1, 0.8, 0.4, 1.0);
        return;
    }

    // Detail-noise coordinates (see common.glsl for why the modulo).
    vec2  p   = vWorld.xz + uOriginMod;
    float n16 = valueNoise(p / 16.0, 256.0);   // 16 m patches
    float n4  = valueNoise(p / 4.0, 1024.0);   //  4 m clumps
    float n1  = valueNoise(p, 4096.0);         //  1 m grain
    float fade = 1.0 - smoothstep(60.0, 400.0, dist); // fine detail only up close

    // Micro relief: tilt the normal by the slope of the 1 m noise.
    vec3 N = normalize(vNormal);
    float e = 0.25;
    float nx = valueNoise((p + vec2(e, 0.0)), 4096.0) - n1;
    float nz = valueNoise((p + vec2(0.0, e)), 4096.0) - n1;
    // Only on gentle ground: on cliffs this xz noise would smear into streaks.
    N = normalize(N + vec3(-nx, 0.0, -nz) * 1.6 * fade * smoothstep(0.6, 0.9, N.y));

    float h     = vWorld.y;
    float slope = 1.0 - normalize(vNormal).y;
    float moist = clamp(vMoisture + (n16 - 0.5) * 0.35, 0.0, 1.0);

    // ---- surface colour ------------------------------------------------------
    vec3 seabed = vec3(0.60, 0.55, 0.41);
    vec3 weed   = vec3(0.16, 0.24, 0.12);
    vec3 sand   = vec3(0.74, 0.66, 0.49);
    vec3 dry    = vec3(0.34, 0.31, 0.16);
    vec3 lush   = vec3(0.12, 0.25, 0.06);
    vec3 forest = vec3(0.045, 0.13, 0.035);
    vec3 rock   = vec3(0.19, 0.175, 0.16);

    // Sea floor: sand with darker weed beds in deeper water.
    vec3 c = mix(seabed, weed, smoothstep(0.55, 0.75, n16) * smoothstep(-2.0, -6.0, h));
    // Beach: the dry/wet line wanders with the 4 m noise.
    float shore = h + (n4 - 0.5) * 0.6;
    c = mix(c, sand, smoothstep(-0.6, 0.5, shore));
    c *= mix(0.7, 1.0, smoothstep(0.0, 1.0, shore));                // wet sand
    vec3 grass = mix(dry, lush, smoothstep(0.3, 0.6, moist)) * (0.8 + 0.4 * n4);
    c = mix(c, grass, smoothstep(1.6, 3.2, h + (n4 - 0.5) * 1.5));
    float forestAmt = smoothstep(0.5, 0.65, moist) * smoothstep(4.0, 9.0, h) * (1.0 - smoothstep(110.0, 150.0, h));
    c = mix(c, forest * (0.75 + 0.5 * n4), forestAmt * smoothstep(0.35, 0.55, n4 + n16 * 0.4));
    // Rock on steep ground and high up, with faint horizontal strata.
    float rockAmt = max(smoothstep(0.22, 0.42, slope + (n4 - 0.5) * 0.12) * smoothstep(0.5, 3.0, h),
                        smoothstep(130.0, 170.0, h));
    // On steep faces, take the noise from (along the face, up the face)
    // instead of (x, z), which would stretch into vertical streaks.
    float nFace = valueNoise(vec2(p.x + p.y, h * 1.5) / 4.0, 1024.0);
    vec3 strata = rock * (0.78 + 0.16 * sin(h * 1.7 + n4 * 3.0) + 0.22 * nFace);
    c = mix(c, strata, rockAmt);
    c *= mix(1.0, 0.85 + 0.3 * n1, fade);                           // grain

    // ---- lighting ------------------------------------------------------------
    vec3 light = surfaceLight(vWorld, N);

    if (h < 0.0) {
        // Sunlight crossing (-h) metres of water on the way down...
        float depth = -h;
        light *= exp(-kWaterAbsorb * depth * 1.5);
        // ...and caustics: wave-focused light dancing on the sea floor.
        vec2 q = p * 0.35;
        float ca = sin(q.x * 2.1 + uTime * 1.1 + sin(q.y * 1.7 + uTime * 0.7))
                 * sin(q.y * 2.3 - uTime * 0.9 + sin(q.x * 1.3 - uTime * 0.6));
        float caustic = pow(abs(ca), 3.0);
        light *= 0.75 + 1.2 * caustic * smoothstep(0.0, 1.0, depth) * exp(-depth * 0.08);
    }

    vec3 col = c * light;

    vec3 rd = (vWorld - uCamPos) / max(dist, 1e-4);
    float fog = fogAmount(dist);
    // Fully fogged before the edge of the streamed area: no visible pop-in.
    fog = max(fog, smoothstep(0.7 * uViewDistance, 0.95 * uViewDistance, dist));
    outColor = vec4(mix(col, fogColor(rd), fog), 1.0);
}
