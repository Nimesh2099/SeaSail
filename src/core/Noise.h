// Noise.h - gradient noise and fractal sums, written from scratch.
//
// Everything takes double-precision coordinates: the sea is endless, and a
// float can't tell 10,000,000.25 m from 10,000,000.5 m.
#pragma once

#include <array>
#include <cstdint>

namespace sea {

class GradientNoise {
public:
    explicit GradientNoise(uint32_t seed = 1337);

    // Ken Perlin's "improved noise" (2002) in 3D. Output roughly [-1, 1].
    float sample(double x, double y, double z) const;

private:
    std::array<uint8_t, 512> perm_{}; // permutation table, doubled to avoid wrapping
};

struct FbmParams {
    int   octaves    = 4;
    float frequency  = 1.0f;  // of the first octave (1 / feature size in metres)
    float lacunarity = 2.0f;  // frequency multiplier per octave
    float gain       = 0.5f;  // amplitude multiplier per octave
};

// Fractional Brownian motion over the (x, z) plane: octaves of noise, each
// finer and fainter. Roughly [-1, 1]. `plane` picks a different horizontal
// slice of the 3D noise, so many independent 2D fields share one table.
float fbm2(const GradientNoise& n, double x, double z, const FbmParams& fp, double plane = 0.0);

// Ridged multifractal: (1 - |noise|)^2 turns zero crossings into sharp
// crests (mountain ridges); each octave is weighted by the previous one so
// valleys stay smooth. Roughly [0, 1].
float ridged2(const GradientNoise& n, double x, double z, const FbmParams& fp, double plane = 0.0);

} // namespace sea
