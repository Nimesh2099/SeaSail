#version 330 core

#include "common.glsl"

in vec3  vWorld;
in vec2  vBase;
in float vCrest;

const int MAX_WAVES = 10;
uniform vec4 uWaveA[MAX_WAVES];
uniform vec4 uWaveB[MAX_WAVES];
uniform int  uWaveCount;

uniform sampler2D uScene;          // HDR colour of everything under/behind the water
uniform sampler2D uSceneDistance;  // camera distance to that surface
uniform vec2  uScreenSize;
uniform int   uWireframe;

// Boat wake: recent stern positions, newest first.
// xy = camera-relative position, z = age in seconds, w = strength 0..1
const int MAX_WAKE = 64;
uniform vec4 uWake[MAX_WAKE];
uniform int  uWakeCount;
uniform vec2 uBoatXZ;
// The wake in groups of 8 segments: xy = centre, z = radius that holds the
// group's foam. Pixels outside a group's circle skip its segments.
const int WAKE_GROUP = 8;
uniform vec4 uWakeGroups[MAX_WAKE / WAKE_GROUP];

out vec4 fragColor;

const vec3  kWaterAbsorb = vec3(0.42, 0.065, 0.04);   // per metre: red dies first
const float kWakeLife = 12.0;

// Per-pixel normal from every wave, including ripples too small to move
// vertices. Derivative of the Gerstner sum.
vec3 waveNormal(vec2 p, float dist)
{
    vec3 n = vec3(0.0, 1.0, 0.0);
    for (int i = 0; i < uWaveCount; ++i) {
        if (dist > uWaveB[i].z) continue;   // ripple smaller than a pixel out here
        vec2  D = uWaveA[i].xy;
        float k = uWaveA[i].z;
        float A = uWaveA[i].w * (1.0 - smoothstep(0.5 * uWaveB[i].z, uWaveB[i].z, dist));
        float theta = k * dot(D, p) + uWaveB[i].y;
        float kA = k * A;
        n.xz -= D * kA * cos(theta);
        n.y  -= uWaveB[i].x * kA * sin(theta);
    }
    return normalize(n);
}

// Foam left behind the boat. Each wake point is a disc whose radius grows
// with age; the union of the discs is a trail that widens behind the boat.
// We find how deep inside that union this pixel is (d / width, smallest
// over all segments), then draw a churned core plus two bright edge lines
// that spread apart, roughly the V of a real wake.
float wakeFoam(vec2 p)
{
    if (uWakeCount < 2 || distance(p, uBoatXZ) > 180.0) return 0.0;
    float best = 1e9, bestAge = 0.0, bestStrength = 0.0;
    int groups = (uWakeCount - 1 + WAKE_GROUP - 1) / WAKE_GROUP;
    for (int g = 0; g < groups; ++g) {
        vec4 G = uWakeGroups[g];
        if (distance(p, G.xy) > G.z) continue;   // nowhere near this stretch of wake
        int last = min(g * WAKE_GROUP + WAKE_GROUP, uWakeCount - 1);
        for (int i = g * WAKE_GROUP; i < last; ++i) {
            vec2 a = uWake[i].xy, b = uWake[i + 1].xy;
            vec2 ab = b - a;
            float t = clamp(dot(p - a, ab) / max(dot(ab, ab), 1e-4), 0.0, 1.0);
            float d = length(p - (a + ab * t));
            float age = mix(uWake[i].z, uWake[i + 1].z, t);
            float strength = mix(uWake[i].w, uWake[i + 1].w, t);
            float width = 1.0 + age * (0.35 + 0.8 * strength);
            float m = d / width;
            if (m < best) { best = m; bestAge = age; bestStrength = strength; }
        }
    }
    if (best > 1.3) return 0.0;
    float life = 1.0 - bestAge / kWakeLife;
    float core = (1.0 - smoothstep(0.1, 0.8, best)) * (1.0 - smoothstep(0.0, 4.0, bestAge)) * 0.7;
    float edge = (1.0 - smoothstep(0.0, 0.18, abs(best - 1.0))) * 0.55;
    return (core + edge) * bestStrength * life * life;
}

void main()
{
    if (uWireframe == 1) {
        fragColor = vec4(0.2, 0.5, 1.0, 1.0);
        return;
    }

    vec3  toFrag    = vWorld - uCamPos;
    float waterDist = length(toFrag);
    vec3  rd        = toFrag / waterDist;
    vec3  V         = -rd;

    // Terrain or the boat in front of the water at this pixel? Not ours.
    vec2  uv = gl_FragCoord.xy / uScreenSize;
    float sceneDist = texture(uSceneDistance, uv).r;
    if (sceneDist < waterDist - 0.05) discard;

    vec3 N = waveNormal(vBase, length(vBase - uCamPos.xz));

    // ---- looking through the water ---------------------------------------------
    float thickness = sceneDist - waterDist;
    vec2  uvR = uv + N.xz * 0.035 * clamp(thickness * 0.3, 0.0, 1.0);   // fake refraction
    float distR = texture(uSceneDistance, uvR).r;
    if (distR < waterDist) { uvR = uv; distR = sceneDist; }
    thickness = max(distR - waterDist, 0.0);

    vec3 behind = texture(uScene, uvR).rgb;
    vec3 T = exp(-kWaterAbsorb * thickness);            // light that survives the trip
    float daylight = uSunIntensity * max(uSunDir.y, 0.0) + 0.06 + 0.4 * length(uMoonLight) + 0.19 * (1.0 - uNight);
    vec3 deep = vec3(0.0, 0.045, 0.07) * daylight;      // light the water scatters back
    vec3 water = behind * T + deep * (1.0 - T);

    // ---- reflection -------------------------------------------------------------
    vec3 R = reflect(rd, N);
    R.y = abs(R.y);
    vec3 reflection = skyWithClouds(R);
    float F = 0.02 + 0.98 * pow(1.0 - clamp(dot(N, V), 0.0, 1.0), 5.0);  // Schlick, water F0 = 0.02
    vec3 col = mix(water, reflection, F);

    float sr = max(dot(R, uSunDir), 0.0);
    col += uSunColor * uSunIntensity * (pow(sr, 900.0) * 6.0 + pow(sr, 120.0) * 0.15);
    float mr = max(dot(R, uMoonDir), 0.0);
    col += uMoonLight * (pow(mr, 600.0) * 4.0 + pow(mr, 60.0) * 0.12);

    // Lamps: a glittering reflection of each light, plus a soft glow where
    // the light falls on the water (the searchlight's cone shows on the sea).
    for (int i = 0; i < uPointCount; ++i) {
        vec3 L = uPointLights[i].xyz - vWorld;
        float d = length(L);
        vec3 l = L / d;
        float att = 1.0 / (1.0 + 0.004 * d * d);
        col += uPointColors[i].rgb * att * (pow(max(dot(R, l), 0.0), 300.0) * 3.0 + pow(max(dot(R, l), 0.0), 25.0) * 0.08);
    }
    // One lamp evaluation shared by the water and its foam (both nearly face up).
    vec3 lamps = localLight(vWorld, vec3(0.0, 1.0, 0.0));
    col += lamps * 0.12;

    // ---- foam -------------------------------------------------------------------
    float pattern = 0.5 + 0.25 * sin(vBase.x * 1.7 + uTime * 1.3) * sin(vBase.y * 1.3 - uTime * 0.9)
                        + 0.25 * sin((vBase.x + vBase.y) * 0.6 - uTime * 1.7);
    // Shallow water: shorelines, and the hull of the boat itself.
    float depthBelow = thickness * max(-rd.y, 0.03);
    float shore = 1.0 - smoothstep(0.0, 0.9, depthBelow);
    float foam = shore * smoothstep(0.35, 0.75, pattern + shore * 0.45);
    // Wave crests.
    foam = max(foam, smoothstep(1.0, 1.4, vCrest) * 0.35 * pattern);
    // The boat's wake.
    float wake = wakeFoam(vBase);
    foam = max(foam, wake * smoothstep(0.15, 0.6, pattern + wake * 0.5));

    vec3 foamColor = vec3(0.92) * (uSunColor * uSunIntensity * max(uSunDir.y, 0.0) * 0.6 + skyColor(vec3(0, 1, 0)) * 0.6
                                   + uMoonLight * 0.4 + lamps * 0.8);
    col = mix(col, foamColor, clamp(foam, 0.0, 1.0));

    col = mix(col, fogColor(rd), fogAmount(waterDist));
    fragColor = vec4(toneMap(col), 1.0);
}
