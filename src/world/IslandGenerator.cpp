#include "IslandGenerator.h"

#include "SeaMath.h"

#include <algorithm>
#include <cmath>

namespace sea {

namespace {

constexpr float kSeabed = -40.0f;

uint32_t mix32(uint32_t x)
{
    x ^= x >> 16; x *= 0x7feb352dU;
    x ^= x >> 15; x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

// Deterministic random number in [0, 1) for a cell, seed and "which number".
float cellRandom(int64_t cx, int64_t cz, uint32_t seed, uint32_t salt)
{
    uint32_t h = mix32(static_cast<uint32_t>(cx) * 0x8da6b343U ^ static_cast<uint32_t>(cx >> 32) * 0x2c1b3c6dU
                     ^ static_cast<uint32_t>(cz) * 0xd8163841U ^ static_cast<uint32_t>(cz >> 32) * 0x297a2d39U
                     ^ seed * 0xcb1ab31fU ^ salt * 0x9e3779b9U);
    return static_cast<float>(mix32(h) >> 8) * (1.0f / 16777216.0f);
}

// Squashes heights near sea level so coasts get wide, flat beaches.
float flattenBeaches(float h)
{
    const float b = 2.5f;
    return h - 0.6f * b * std::tanh(h / b);
}

// Noise fields every island shares at one point; computed once per sample.
struct SharedFields {
    float seabed, warpX, warpZ, hills, ridges, fine, channels;
};

} // namespace

IslandGenerator::IslandGenerator(uint32_t seed) : seed_(seed), noise_(seed) {}

IslandInfo IslandGenerator::islandInCell(int64_t cx, int64_t cz) const
{
    IslandInfo is;
    if (cellRandom(cx, cz, seed_, 0) > 0.62f) return is;

    // Keep centres away from cell borders, so a point only ever needs to
    // check its own cell and the 8 around it.
    is.cx = (static_cast<double>(cx) + 0.25 + 0.5 * cellRandom(cx, cz, seed_, 1)) * kCellSize;
    is.cz = (static_cast<double>(cz) + 0.25 + 0.5 * cellRandom(cx, cz, seed_, 2)) * kCellSize;

    float kind = cellRandom(cx, cz, seed_, 3);
    float size = cellRandom(cx, cz, seed_, 4);
    float r3   = cellRandom(cx, cz, seed_, 5);
    size *= size; // small islands common, big ones rare

    if (kind < 0.14f) {
        is.type = IslandType::Atoll;
        is.radius = lerpf(260.0f, 620.0f, size);
        is.peak = 3.5f;
    } else if (kind < 0.30f) {
        is.type = IslandType::SeaStacks;
        is.radius = lerpf(140.0f, 320.0f, size);
        is.peak = lerpf(18.0f, 45.0f, r3);
    } else if (kind < 0.46f) {
        is.type = IslandType::Cay;
        is.radius = lerpf(110.0f, 240.0f, size);
        is.peak = 2.5f;
    } else {
        is.type = IslandType::Mountain;
        is.radius = lerpf(240.0f, 850.0f, size);
        is.peak = is.radius * lerpf(0.10f, 0.26f, r3);
        // Half the islands have beaches all round; the rest get cliffs.
        float c = cellRandom(cx, cz, seed_, 6);
        is.cliffs = c < 0.5f ? 0.0f : (c - 0.5f) * 2.0f;
    }
    return is;
}

IslandInfo IslandGenerator::nearestIsland(double x, double z, IslandType wanted, int maxRings) const
{
    const int64_t ccx = static_cast<int64_t>(std::floor(x / kCellSize));
    const int64_t ccz = static_cast<int64_t>(std::floor(z / kCellSize));
    IslandInfo best;
    double bestD2 = 1e300;
    for (int ring = 0; ring <= maxRings; ++ring) {
        for (int64_t dz = -ring; dz <= ring; ++dz)
            for (int64_t dx = -ring; dx <= ring; ++dx) {
                if (dx != -ring && dx != ring && dz != -ring && dz != ring) continue; // ring border only
                IslandInfo is = islandInCell(ccx + dx, ccz + dz);
                if (is.type == IslandType::None) continue;
                if (wanted != IslandType::None && is.type != wanted) continue;
                double d2 = (is.cx - x) * (is.cx - x) + (is.cz - z) * (is.cz - z);
                if (d2 < bestD2) { bestD2 = d2; best = is; }
            }
        if (best.type != IslandType::None) return best;
    }
    return best;
}

TerrainSample IslandGenerator::sample(double x, double z) const
{
    const GradientNoise& n = noise_;
    SharedFields f;
    f.seabed   = kSeabed + 9.0f * fbm2(n, x, z, {3, 1.0f / 700.0f}, 0.0);
    f.warpX    = fbm2(n, x, z, {3, 1.0f / 420.0f}, 100.0);
    f.warpZ    = fbm2(n, x, z, {3, 1.0f / 420.0f}, 200.0);
    f.hills    = fbm2(n, x, z, {4, 1.0f / 260.0f}, 300.0);
    f.ridges   = ridged2(n, x, z, {5, 1.0f / 380.0f, 2.1f}, 400.0);
    f.fine     = fbm2(n, x, z, {3, 1.0f / 22.0f}, 500.0);
    f.channels = fbm2(n, x, z, {2, 1.0f / 160.0f}, 700.0);

    const int64_t ccx = static_cast<int64_t>(std::floor(x / kCellSize));
    const int64_t ccz = static_cast<int64_t>(std::floor(z / kCellSize));

    float h = f.seabed;
    for (int64_t dz = -1; dz <= 1; ++dz)
        for (int64_t dx = -1; dx <= 1; ++dx) {
            IslandInfo is = islandInCell(ccx + dx, ccz + dz);
            if (is.type == IslandType::None) continue;

            // Domain warp: bends the island's circular outline into bays
            // and headlands. Atolls stay rounder.
            float warp = is.type == IslandType::Atoll ? 0.25f : 0.7f;
            float px = static_cast<float>(x - is.cx) + f.warpX * is.radius * warp;
            float pz = static_cast<float>(z - is.cz) + f.warpZ * is.radius * warp;
            float d = std::sqrt(px * px + pz * pz);
            if (d >= is.radius) continue;
            float t = 1.0f - d / is.radius;  // 0 at the outer edge, 1 at the centre

            float ih = f.seabed;
            switch (is.type) {
            case IslandType::Mountain: {
                // Steep underwater slope in the outer part, gentle rise inland.
                float s = smoothstepf(0.0f, 0.55f, t);
                float top = is.peak * (0.55f + 0.45f * t) * (0.8f + 0.6f * f.hills);
                ih = lerpf(f.seabed, top, s)
                   + f.ridges * is.peak * 0.55f * smoothstepf(0.45f, 0.9f, t)
                   + f.fine * 1.6f * smoothstepf(0.25f, 0.5f, t);
                // Cliff islands: a sea cliff 8-35 m tall rises straight out
                // of the water near the coastline; inland, the normal hills
                // take over once they climb higher than the cliff top.
                if (is.cliffs > 0.0f) {
                    float cliffTop = is.cliffs * (14.0f + 22.0f * (0.5f + 0.5f * f.hills));
                    float edge = 0.22f + 0.05f * f.channels;  // the cliff line wanders along the coast
                    float rise = smoothstepf(edge - 0.012f, edge + 0.012f, t);
                    if (rise > 0.0f) ih = std::max(ih, (cliffTop + f.fine * 2.0f) * rise);
                }
                break;
            }
            case IslandType::Atoll: {
                // Shallow lagoon inside, a low ring of sand around it, broken
                // by channels where the noise dips.
                float lagoon = -3.5f + 1.5f * f.hills;
                float base = lerpf(f.seabed, lagoon, smoothstepf(0.10f, 0.28f, t));
                float ring = smoothstepf(0.0f, 1.0f, 1.0f - std::fabs(t - 0.30f) / 0.09f);
                float ringTop = 1.8f + 2.0f * (0.5f + 0.5f * f.hills) - 6.0f * smoothstepf(-0.15f, -0.35f, f.channels);
                ih = std::max(base, lerpf(base, ringTop, ring)) + f.fine * 0.4f;
                break;
            }
            case IslandType::SeaStacks: {
                // A shallow rocky shelf with steep pillars poking out of it.
                float shelf = lerpf(f.seabed, -5.0f + 2.0f * f.hills, smoothstepf(0.0f, 0.5f, t));
                float pillars = smoothstepf(0.10f, 0.24f, f.fine * 0.6f + f.channels * 0.7f);
                ih = shelf + pillars * (is.peak + 5.0f) * smoothstepf(0.15f, 0.45f, t) * (0.7f + 0.5f * f.ridges);
                break;
            }
            case IslandType::Cay: {
                float s = smoothstepf(0.0f, 0.6f, t);
                ih = lerpf(f.seabed, is.peak + 1.2f * f.hills, s) + f.fine * 0.3f;
                break;
            }
            case IslandType::None: break;
            }
            if (ih > h) h = ih; // overlapping islands: highest ground wins
        }

    h = flattenBeaches(h);

    float moisture = clampf(0.5f + fbm2(n, x, z, {3, 1.0f / 520.0f}, 600.0) * 1.4f, 0.0f, 1.0f);
    return {h, moisture};
}

} // namespace sea
