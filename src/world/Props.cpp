#include "Props.h"

#include <algorithm>
#include <cmath>

namespace sea {

namespace {

constexpr double kLattice = 4.0;  // metres between candidate spots

uint32_t hash3(int64_t x, int64_t z, uint32_t seed)
{
    uint32_t h = static_cast<uint32_t>(x) * 0x8da6b343U ^ static_cast<uint32_t>(x >> 32) * 0x68e31da4U
               ^ static_cast<uint32_t>(z) * 0xd8163841U ^ static_cast<uint32_t>(z >> 32) * 0xb5297a4dU
               ^ seed * 0xcb1ab31fU;
    h ^= h >> 16; h *= 0x7feb352dU;
    h ^= h >> 15; h *= 0x846ca68bU;
    h ^= h >> 16;
    return h;
}

// Successive random numbers in [0, 1) from one cell hash.
struct CellRandom {
    uint32_t state;
    float next()
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return static_cast<float>(state >> 8) * (1.0f / 16777216.0f);
    }
};

} // namespace

float HeightGridView::heightAt(float x, float z, Vec3* normal) const
{
    // Which cell, and where inside it.
    float gx = clampf(x / step, 0.0f, static_cast<float>(cells) - 1e-4f);
    float gz = clampf(z / step, 0.0f, static_cast<float>(cells) - 1e-4f);
    int i = static_cast<int>(gx), j = static_cast<int>(gz);
    float fx = gx - static_cast<float>(i), fz = gz - static_cast<float>(j);
    float h00 = h(i, j), h10 = h(i + 1, j), h01 = h(i, j + 1), h11 = h(i + 1, j + 1);

    // The mesh splits each cell along the 10-01 diagonal; interpolate on the
    // same triangle the renderer draws, so nothing floats or sinks.
    float y, dhdx, dhdz;
    if (fx + fz <= 1.0f) {
        dhdx = (h10 - h00) / step;
        dhdz = (h01 - h00) / step;
        y = h00 + (h10 - h00) * fx + (h01 - h00) * fz;
    } else {
        dhdx = (h11 - h01) / step;
        dhdz = (h11 - h10) / step;
        y = h11 + (h01 - h11) * (1.0f - fx) + (h10 - h11) * (1.0f - fz);
    }
    if (normal) *normal = normalize(Vec3{-dhdx, 1.0f, -dhdz});
    return y;
}

float HeightGridView::moistureAt(float x, float z) const
{
    float gx = clampf(x / step, 0.0f, static_cast<float>(cells) - 1e-4f);
    float gz = clampf(z / step, 0.0f, static_cast<float>(cells) - 1e-4f);
    int i = static_cast<int>(gx), j = static_cast<int>(gz);
    float fx = gx - static_cast<float>(i), fz = gz - static_cast<float>(j);
    return lerpf(lerpf(m(i, j), m(i + 1, j), fx), lerpf(m(i, j + 1), m(i + 1, j + 1), fx), fz);
}

void scatterProps(const HeightGridView& grid, double worldX0, double worldZ0, double chunkSize,
                  uint32_t seed, PropLists& out)
{
    for (auto& list : out) list.clear();

    const int64_t cx0 = static_cast<int64_t>(std::floor(worldX0 / kLattice));
    const int64_t cz0 = static_cast<int64_t>(std::floor(worldZ0 / kLattice));
    const int per = static_cast<int>(chunkSize / kLattice);

    for (int b = 0; b < per; ++b)
        for (int a = 0; a < per; ++a) {
            CellRandom rnd{hash3(cx0 + a, cz0 + b, seed) | 1u};
            float x = static_cast<float>((a + rnd.next()) * kLattice);
            float z = static_cast<float>((b + rnd.next()) * kLattice);

            Vec3 n;
            float h = grid.heightAt(x, z, &n);
            if (h < -1.6f) continue;               // open water: nothing
            float slope = 1.0f - n.y;
            float m = grid.moistureAt(x, z);

            float pick = rnd.next(), pick2 = rnd.next(), size = rnd.next();
            PropInstance p{x, h, z, 1.0f, rnd.next() * 2.0f * kPi, 0.82f + 0.36f * rnd.next(), rnd.next() * 6.28f, 0.0f};
            auto add = [&](PropType t, float minScale, float maxScale, float sink) {
                p.scale = lerpf(minScale, maxScale, size);
                p.y = h - sink * p.scale;  // bury the base a little so slopes don't show a gap
                out[static_cast<int>(t)].push_back(p);
            };

            // Boulders scattered along the waterline.
            if (h < 1.2f) {
                if (pick < 0.03f) add(PropType::Rock, 0.8f, 2.2f, 0.35f);
                continue;
            }
            // Cliffs and steep slopes: bare, with the odd rock.
            if (slope > 0.45f) {
                if (pick < 0.08f) add(PropType::Rock, 0.6f, 1.8f, 0.4f);
                continue;
            }
            // Palms like the coast: thick on the beach, thinning inland.
            float palmZone = (1.0f - smoothstepf(6.0f, 16.0f, h)) * smoothstepf(0.2f, 0.4f, m) * (1.0f - smoothstepf(0.2f, 0.32f, slope));
            if (pick < 0.11f * palmZone) { add(PropType::Palm, 0.8f, 1.25f, 0.2f); continue; }

            // Forest where it's moist; pines take over higher up.
            float forest = smoothstepf(0.42f, 0.62f, m) * smoothstepf(3.0f, 7.0f, h) * (1.0f - smoothstepf(140.0f, 170.0f, h));
            if (pick < 0.34f * forest) {
                bool pine = h > lerpf(45.0f, 75.0f, pick2);
                add(pine ? PropType::Pine : PropType::Broadleaf, 0.7f, 1.3f, 0.3f);
                continue;
            }
            // Scrub on drier ground, and the occasional rock in the grass.
            if (h > 1.5f && pick < 0.34f * forest + 0.07f + 0.08f * m) { add(PropType::Bush, 0.6f, 1.4f, 0.25f); continue; }
            if (pick > 0.988f) add(PropType::Rock, 0.4f, 1.2f, 0.35f);
        }
}

} // namespace sea
