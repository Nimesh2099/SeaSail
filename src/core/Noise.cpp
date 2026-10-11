#include "Noise.h"

#include "SeaMath.h"

#include <cmath>
#include <numeric>

namespace sea {

namespace {

// Small deterministic PRNG, so a seed gives the same world on every
// compiler (std::shuffle's output is implementation-defined).
uint32_t xorshift32(uint32_t& s)
{
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

// Quintic fade 6t^5 - 15t^4 + 10t^3: zero first and second derivative at
// t = 0 and 1, so there are no creases at lattice cell borders.
inline float fade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }

// Dot product of (x, y, z) with one of 12 gradient directions.
inline float grad(int hash, float x, float y, float z)
{
    int h = hash & 15;
    float u = h < 8 ? x : y;
    float v = h < 4 ? y : (h == 12 || h == 14 ? x : z);
    return ((h & 1) ? -u : u) + ((h & 2) ? -v : v);
}

} // namespace

GradientNoise::GradientNoise(uint32_t seed)
{
    std::array<uint8_t, 256> p{};
    std::iota(p.begin(), p.end(), 0);
    uint32_t s = seed ? seed : 1u;
    for (int i = 255; i > 0; --i) { // Fisher-Yates
        int j = static_cast<int>(xorshift32(s) % static_cast<uint32_t>(i + 1));
        std::swap(p[i], p[j]);
    }
    for (int i = 0; i < 512; ++i) perm_[i] = p[i & 255];
}

float GradientNoise::sample(double px, double py, double pz) const
{
    // Lattice cell found in double precision...
    double fx = std::floor(px), fy = std::floor(py), fz = std::floor(pz);
    int X = static_cast<int>(static_cast<int64_t>(fx) & 255);
    int Y = static_cast<int>(static_cast<int64_t>(fy) & 255);
    int Z = static_cast<int>(static_cast<int64_t>(fz) & 255);

    // ...position inside it is small, so float is plenty.
    float x = static_cast<float>(px - fx), y = static_cast<float>(py - fy), z = static_cast<float>(pz - fz);
    float u = fade(x), v = fade(y), w = fade(z);

    const auto& P = perm_;
    int A = P[X] + Y,     AA = P[A] + Z, AB = P[A + 1] + Z;
    int B = P[X + 1] + Y, BA = P[B] + Z, BB = P[B + 1] + Z;

    return lerpf(
        lerpf(lerpf(grad(P[AA],     x,     y,     z), grad(P[BA],     x - 1, y,     z), u),
              lerpf(grad(P[AB],     x,     y - 1, z), grad(P[BB],     x - 1, y - 1, z), u), v),
        lerpf(lerpf(grad(P[AA + 1], x,     y,     z - 1), grad(P[BA + 1], x - 1, y,     z - 1), u),
              lerpf(grad(P[AB + 1], x,     y - 1, z - 1), grad(P[BB + 1], x - 1, y - 1, z - 1), u), v),
        w);
}

float fbm2(const GradientNoise& n, double x, double z, const FbmParams& fp, double plane)
{
    float sum = 0.0f, amp = 1.0f, norm = 0.0f;
    double freq = fp.frequency;
    for (int i = 0; i < fp.octaves; ++i) {
        // Per-octave offsets stop every octave sharing a zero at the origin.
        sum  += amp * n.sample(x * freq + 17.13 * i, plane + 31.71 * i, z * freq + 7.37 * i);
        norm += amp;
        amp  *= fp.gain;
        freq *= fp.lacunarity;
    }
    return sum / norm;
}

float ridged2(const GradientNoise& n, double x, double z, const FbmParams& fp, double plane)
{
    float sum = 0.0f, amp = 1.0f, norm = 0.0f, weight = 1.0f;
    double freq = fp.frequency;
    for (int i = 0; i < fp.octaves; ++i) {
        float r = 1.0f - std::fabs(n.sample(x * freq + 5.21 * i, plane + 11.9 * i, z * freq + 23.3 * i));
        r *= r;
        r *= weight;
        weight = clampf(r * 2.0f, 0.0f, 1.0f);
        sum  += amp * r;
        norm += amp;
        amp  *= fp.gain;
        freq *= fp.lacunarity;
    }
    return sum / norm;
}

} // namespace sea
