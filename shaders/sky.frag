#version 330 core

#include "common.glsl"

in vec2 vNdc;

uniform vec3  uCamRight;
uniform vec3  uCamUp;
uniform vec3  uCamForward;
uniform float uTanHalfFov;
uniform float uAspect;

layout(location = 0) out vec4  outColor;
layout(location = 1) out float outDistance;

float hash13(vec3 p)
{
    p = fract(p * 0.1031);
    p += dot(p, p.zyx + 31.32);
    return fract((p.x + p.y) * p.z);
}

// Stars: chop direction space into small cells and light up a few of them.
vec3 stars(vec3 rd)
{
    const float density = 380.0;
    vec3 q = rd * density;
    vec3 cell = floor(q);
    float h = hash13(cell);
    if (h < 0.9965) return vec3(0.0);
    vec3 centre = cell + 0.5 + (vec3(hash13(cell + 1.7), hash13(cell + 4.3), hash13(cell + 9.1)) - 0.5) * 0.6;
    float d = length(q - centre);
    float twinkle = 0.75 + 0.25 * sin(uTime * (2.0 + 6.0 * hash13(cell + 3.3)) + h * 50.0);
    float bright = pow((h - 0.9965) / 0.0035, 3.0) * 1.6 + 0.08;
    vec3 tint = mix(vec3(0.75, 0.82, 1.0), vec3(1.0, 0.88, 0.75), hash13(cell + 2.2));
    return tint * bright * twinkle * smoothstep(0.5, 0.0, d);
}

void main()
{
    vec3 rd = normalize(uCamForward
                        + vNdc.x * uTanHalfFov * uAspect * uCamRight
                        + vNdc.y * uTanHalfFov * uCamUp);

    vec3 col = rd.y > 0.0 ? skyColor(rd) : fogColor(rd);

    if (rd.y > 0.0) {
        // Behind the clouds: stars, the moon and the sun.
        float horizonFade = smoothstep(0.0, 0.12, rd.y);
        col += stars(rd) * uNight * horizonFade * 0.6;

        float md = dot(rd, uMoonDir);
        if (md > 0.9990) {
            // Moon disc with a few darker "seas" from value noise.
            vec3 u = normalize(cross(uMoonDir, vec3(0, 1, 0)));
            vec3 v = cross(u, uMoonDir);
            vec2 uv = vec2(dot(rd, u), dot(rd, v)) * 900.0;
            float maria = valueNoise(uv * 0.12 + 3.0, 1024.0) * 0.6 + valueNoise(uv * 0.35, 1024.0) * 0.4;
            vec3 moon = vec3(0.95, 0.94, 0.88) * mix(1.0, 0.65, smoothstep(0.45, 0.7, maria));
            col = mix(col, moon * mix(0.4, 2.2, uNight), smoothstep(0.99905, 0.99925, md) * horizonFade);
        }

        float d = dot(rd, uSunDir);
        col += uSunColor * smoothstep(0.99965, 0.99985, d) * 25.0;

        vec4 c = clouds(rd);
        col = mix(col, c.rgb, c.a);
    }

    outColor = vec4(col, 1.0);
    outDistance = 1e6; // "nothing here": the water treats this as bottomless
}
