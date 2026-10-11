// common.glsl - sky, clouds, lighting, fog, tone mapping and detail noise,
// shared by every pass through our tiny #include.

uniform vec3  uCamPos;        // camera position, camera-relative world space
uniform vec3  uSunDir;        // unit vector toward the sun (below the horizon at night)
uniform vec3  uSunColor;      // tinted orange when the sun is low
uniform float uSunIntensity;
uniform vec3  uMoonDir;
uniform vec3  uMoonLight;     // moon colour * intensity (zero by day)
uniform float uNight;         // 0 = day, 1 = full night
uniform float uFogDensity;    // per metre
uniform float uViewDistance;  // terrain streaming radius, metres
uniform float uExposure;
uniform float uTime;          // seconds, wrapped (animation only)
uniform vec2  uOriginMod;     // floating origin modulo 4096 m (see detail noise)
uniform vec2  uCloudOffset;   // cloud drift, metres, modulo 4096

// Local lights: the boat's lamps, lighthouses. xyz = camera-relative
// position, w = range in metres; colour already includes intensity.
const int MAX_POINT_LIGHTS = 6;
uniform vec4 uPointLights[MAX_POINT_LIGHTS];
uniform vec4 uPointColors[MAX_POINT_LIGHTS];
uniform int  uPointCount;
uniform vec3 uSpotPos;        // the boat's searchlight
uniform vec3 uSpotDir;
uniform vec3 uSpotColor;

// ---- detail noise -----------------------------------------------------------
// World coordinates are huge on an endless sea, so shaders get positions as
// (camera-relative position + uOriginMod). The noise lattice repeats every
// `period` cells, and 4096 m is a whole number of cells at every frequency
// we use, so patterns line up exactly when the origin moves.

float hash12(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float valueNoise(vec2 p, float period)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    float a = hash12(mod(i, period));
    float b = hash12(mod(i + vec2(1.0, 0.0), period));
    float c = hash12(mod(i + vec2(0.0, 1.0), period));
    float d = hash12(mod(i + vec2(1.0, 1.0), period));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

// ---- sky --------------------------------------------------------------------

// Analytic sky: a horizon-to-zenith gradient plus glows around the sun and
// moon. Day, sunset and night palettes blend on the sun's height. It also
// *is* the fog colour at the horizon, so distant things melt into it.
vec3 skyColor(vec3 rd)
{
    float sunUp = smoothstep(-0.05, 0.35, uSunDir.y);
    float day   = smoothstep(-0.20, 0.04, uSunDir.y);

    vec3 zenithDay  = vec3(0.09, 0.22, 0.55) * mix(0.45, 1.0, sunUp);
    vec3 horizonDay = mix(vec3(0.85, 0.50, 0.32) * 0.75, vec3(0.58, 0.70, 0.86), sunUp);
    vec3 zenith  = mix(vec3(0.004, 0.008, 0.022), zenithDay, day);
    vec3 horizon = mix(vec3(0.020, 0.030, 0.055), horizonDay, day);

    float y = max(rd.y, 0.0);
    vec3 col = mix(horizon, zenith, pow(y, 0.5));

    float s = max(dot(rd, uSunDir), 0.0);
    col += uSunColor * (0.20 * pow(s, 6.0) + 0.6 * pow(s, 64.0)) * day;
    float m = max(dot(rd, uMoonDir), 0.0);
    col += vec3(0.5, 0.6, 0.8) * 0.06 * pow(m, 24.0) * uNight;
    return col;
}

// What distant things fade into: the sky at the horizon in that direction.
vec3 fogColor(vec3 rd)
{
    vec2 h = rd.xz;
    float len = length(h);
    vec3 flat_ = len > 1e-4 ? vec3(h.x / len, 0.0, h.y / len) : vec3(1.0, 0.0, 0.0);
    return skyColor(flat_);
}

float fogAmount(float dist)
{
    return 1.0 - exp(-dist * uFogDensity);
}

// A layer of clouds 1.2 km up. rgb = colour, a = coverage. Four octaves of
// value noise drifting with the wind; lit from above by the sun or moon.
// `detail` = false skips the two finest octaves (using their average
// instead), for reflections where the ripples hide fine detail anyway.
vec4 clouds(vec3 rd, bool detail)
{
    if (rd.y < 0.01) return vec4(0.0);
    float t = (1200.0 - uCamPos.y) / rd.y;
    vec2 p = uCamPos.xz + rd.xz * t + uOriginMod + uCloudOffset;
    float n = valueNoise(p / 512.0, 8.0) * 0.5
            + valueNoise(p / 256.0, 16.0) * 0.25;
    n += detail ? valueNoise(p / 128.0, 32.0) * 0.125 + valueNoise(p / 64.0, 64.0) * 0.0625
                : 0.5 * (0.125 + 0.0625);
    n /= 0.9375;
    float cover = smoothstep(0.52, 0.72, n);
    if (cover <= 0.0) return vec4(0.0);

    float day = smoothstep(-0.20, 0.04, uSunDir.y);
    // Thicker parts are darker underneath; edges catch the light.
    float thick = smoothstep(0.55, 0.9, n);
    vec3 lit = uSunColor * uSunIntensity * 0.32 * day + uMoonLight * 0.4 + skyColor(vec3(0, 1, 0)) * 0.9;
    vec3 col = lit * mix(1.05, 0.55, thick);
    // Silver lining toward the sun.
    col += uSunColor * pow(max(dot(rd, uSunDir), 0.0), 12.0) * 0.8 * day * (1.0 - thick);
    // Fade toward the horizon, where the layer is far away and thin.
    float a = cover * smoothstep(0.01, 0.15, rd.y) * 0.92;
    return vec4(col, a);
}

// Sky plus clouds, as reflected in the water (coarse clouds).
vec3 skyWithClouds(vec3 rd)
{
    vec3 col = skyColor(rd);
    vec4 c = clouds(rd, false);
    return mix(col, c.rgb, c.a);
}

// ---- lighting ---------------------------------------------------------------

// Diffuse light from the boat's lamps and any lighthouses nearby.
vec3 localLight(vec3 P, vec3 N)
{
    vec3 sum = vec3(0.0);
    for (int i = 0; i < uPointCount; ++i) {
        vec3 L = uPointLights[i].xyz - P;
        float d = length(L);
        float range = uPointLights[i].w;
        if (d > range) continue;
        float window = 1.0 - (d / range) * (d / range);
        float att = window * window / (1.0 + 0.08 * d * d);
        sum += uPointColors[i].rgb * att * max(dot(N, L / d), 0.0);
    }
    // Searchlight: a cone in front of the boat.
    vec3 Ls = uSpotPos - P;
    float ds = length(Ls);
    if (ds < 90.0 && dot(uSpotColor, uSpotColor) > 0.0) {
        vec3 l = Ls / ds;
        float cone = smoothstep(0.86, 0.95, dot(-l, uSpotDir));
        float att = (1.0 - ds / 90.0) / (1.0 + 0.004 * ds * ds);
        sum += uSpotColor * cone * att * max(dot(N, l), 0.0);
    }
    return sum;
}

// Everything that lights an opaque surface: sun, moon, sky, lamps.
vec3 surfaceLight(vec3 P, vec3 N)
{
    vec3 light = uSunColor * uSunIntensity * max(dot(N, uSunDir), 0.0)
               + uMoonLight * max(dot(N, uMoonDir), 0.0)
               + skyColor(vec3(0.0, 1.0, 0.0)) * (0.35 + 0.35 * N.y)
               + vec3(0.010, 0.014, 0.026);   // a floor so night is never pure black
    return light + localLight(P, N);
}

// Narkowicz's fit of the ACES filmic curve, then linear -> sRGB.
vec3 toneMap(vec3 c)
{
    c *= uExposure;
    c = clamp((c * (2.51 * c + 0.03)) / (c * (2.43 * c + 0.59) + 0.14), 0.0, 1.0);
    return pow(c, vec3(1.0 / 2.2));
}
