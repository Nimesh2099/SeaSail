#version 330 core

#include "common.glsl"

// Tone-maps the HDR scene onto the screen, with FXAA (fast approximate
// anti-aliasing) to smooth jagged edges: find the direction of the local
// edge from the brightness of the four diagonal neighbours, then blur a
// little along that edge, never across it.

in vec2 vNdc;

uniform sampler2D uScene;
uniform vec2 uScreenSize;

out vec4 fragColor;

vec3 ldr(vec2 uv) { return toneMap(texture(uScene, uv).rgb); }

void main()
{
    vec2 uv = vNdc * 0.5 + 0.5;
    vec2 px = 1.0 / uScreenSize;

    vec3 rgbNW = ldr(uv + vec2(-1.0, -1.0) * px);
    vec3 rgbNE = ldr(uv + vec2( 1.0, -1.0) * px);
    vec3 rgbSW = ldr(uv + vec2(-1.0,  1.0) * px);
    vec3 rgbSE = ldr(uv + vec2( 1.0,  1.0) * px);
    vec3 rgbM  = ldr(uv);

    const vec3 toLuma = vec3(0.299, 0.587, 0.114);
    float lNW = dot(rgbNW, toLuma), lNE = dot(rgbNE, toLuma);
    float lSW = dot(rgbSW, toLuma), lSE = dot(rgbSE, toLuma);
    float lM  = dot(rgbM, toLuma);
    float lMin = min(lM, min(min(lNW, lNE), min(lSW, lSE)));
    float lMax = max(lM, max(max(lNW, lNE), max(lSW, lSE)));

    // No real edge here (most of the screen): keep the pixel as it is and
    // skip the four extra samples along the edge.
    if (lMax - lMin < max(0.0312, lMax * 0.125)) {
        fragColor = vec4(rgbM, 1.0);
        return;
    }

    // Edge direction (perpendicular to the brightness gradient).
    vec2 dir = vec2(-((lNW + lNE) - (lSW + lSE)), (lNW + lSW) - (lNE + lSE));
    float reduce = max((lNW + lNE + lSW + lSE) * 0.25 * (1.0 / 8.0), 1.0 / 128.0);
    float rcpMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + reduce);
    dir = clamp(dir * rcpMin, vec2(-8.0), vec2(8.0)) * px;

    vec3 rgbA = 0.5 * (ldr(uv + dir * (1.0 / 3.0 - 0.5)) + ldr(uv + dir * (2.0 / 3.0 - 0.5)));
    vec3 rgbB = rgbA * 0.5 + 0.25 * (ldr(uv - dir * 0.5) + ldr(uv + dir * 0.5));
    float lB = dot(rgbB, toLuma);

    // If the wider blur picked up something outside the local range, it
    // crossed another edge: use the narrower one.
    fragColor = vec4((lB < lMin || lB > lMax) ? rgbA : rgbB, 1.0);
}
