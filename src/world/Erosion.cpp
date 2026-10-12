#include "Erosion.h"

#include "SeaMath.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace sea {

bool ErosionMap::lookup(double x, double z, float& outDh, float& outFlow) const
{
    double gx = (x - x0) / cell, gz = (z - z0) / cell;
    if (gx < 1.0 || gz < 1.0 || gx >= n - 2 || gz >= n - 2) return false;
    const int ix = static_cast<int>(gx), iz = static_cast<int>(gz);
    const float fx = static_cast<float>(gx - ix), fz = static_cast<float>(gz - iz);
    auto at = [&](const std::vector<float>& g, int i, int j) { return g[static_cast<size_t>(j) * n + i]; };

    // Height change: a cubic B-spline over the 4x4 points around (x, z).
    // Plain bilinear blending is only continuous in value, not in slope, so
    // the lit terrain showed a crease along every grid line; the B-spline is
    // smooth in slope and curvature too (and softens the grid a little).
    auto weights = [](float t, float w[4]) {
        const float t2 = t * t, t3 = t2 * t;
        w[0] = (1.0f - t) * (1.0f - t) * (1.0f - t) / 6.0f;
        w[1] = (3.0f * t3 - 6.0f * t2 + 4.0f) / 6.0f;
        w[2] = (-3.0f * t3 + 3.0f * t2 + 3.0f * t + 1.0f) / 6.0f;
        w[3] = t3 / 6.0f;
    };
    float wx[4], wz[4];
    weights(fx, wx);
    weights(fz, wz);
    float sum = 0.0f;
    for (int j = 0; j < 4; ++j)
        for (int i = 0; i < 4; ++i) sum += wx[i] * wz[j] * at(dh, ix - 1 + i, iz - 1 + j);
    outDh = sum;

    // Flow only tints moisture, so bilinear is plenty.
    float a = lerpf(at(flow, ix, iz), at(flow, ix + 1, iz), fx);
    float b = lerpf(at(flow, ix, iz + 1), at(flow, ix + 1, iz + 1), fx);
    outFlow = lerpf(a, b, fz);
    return true;
}

namespace {

// The heightfield the droplets run over. Heights are stored in *cells*
// (metres / cell size), so a slope of 1 is 45 degrees whatever the grid
// resolution: the same settings work for small and large islands.
struct Field {
    int n;
    std::vector<float> h;
    float& at(int i, int j) { return h[static_cast<size_t>(j) * n + i]; }
    float at(int i, int j) const { return h[static_cast<size_t>(j) * n + i]; }

    // Height and gradient at a fractional position (bilinear).
    void sample(float px, float pz, float& height, float& gx, float& gz) const
    {
        int i = static_cast<int>(px), j = static_cast<int>(pz);
        float u = px - static_cast<float>(i), v = pz - static_cast<float>(j);
        float h00 = at(i, j), h10 = at(i + 1, j), h01 = at(i, j + 1), h11 = at(i + 1, j + 1);
        gx = (h10 - h00) * (1.0f - v) + (h11 - h01) * v;
        gz = (h01 - h00) * (1.0f - u) + (h11 - h10) * u;
        height = h00 * (1 - u) * (1 - v) + h10 * u * (1 - v) + h01 * (1 - u) * v + h11 * u * v;
    }

    // Spread `amount` over the 4 cells around a point (bilinear weights).
    void deposit(float px, float pz, float amount)
    {
        int i = static_cast<int>(px), j = static_cast<int>(pz);
        float u = px - static_cast<float>(i), v = pz - static_cast<float>(j);
        at(i, j)         += amount * (1 - u) * (1 - v);
        at(i + 1, j)     += amount * u * (1 - v);
        at(i, j + 1)     += amount * (1 - u) * v;
        at(i + 1, j + 1) += amount * u * v;
    }
};

struct BrushCell { int di, dj; float w; };

std::vector<BrushCell> makeBrush(int radius)
{
    std::vector<BrushCell> b;
    float sum = 0.0f;
    for (int dj = -radius; dj <= radius; ++dj)
        for (int di = -radius; di <= radius; ++di) {
            float d = std::sqrt(static_cast<float>(di * di + dj * dj));
            if (d > radius) continue;
            float w = 1.0f - d / (static_cast<float>(radius) + 0.5f);
            b.push_back({di, dj, w});
            sum += w;
        }
    for (BrushCell& c : b) c.w /= sum;
    return b;
}

uint64_t splitmix(uint64_t x)
{
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

} // namespace

ErosionMap erodeIsland(const IslandInfo& is, const std::function<float(double, double)>& base, uint32_t seed,
                       const ErosionSettings& s)
{
    ErosionMap m;
    const int n = s.gridSize;
    // Domain warping can push the coast out past the nominal radius.
    const double half = is.radius * 1.45;
    m.n = n;
    m.cell = 2.0 * half / (n - 1);
    m.x0 = is.cx - half;
    m.z0 = is.cz - half;
    const float cell = static_cast<float>(m.cell);

    // ---- 1. sample the base terrain -------------------------------------------
    Field f{n, std::vector<float>(static_cast<size_t>(n) * n)};
    std::vector<float> h0(f.h.size());
    std::vector<int> land;
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) {
            float h = base(m.x0 + i * m.cell, m.z0 + j * m.cell);
            h0[static_cast<size_t>(j) * n + i] = h;
            f.at(i, j) = h / cell;
            if (h > 1.0f && i > 2 && j > 2 && i < n - 3 && j < n - 3) land.push_back(j * n + i);
        }

    std::vector<float> visits(f.h.size(), 0.0f);
    if (!land.empty()) {
        // ---- 2. rain ----------------------------------------------------------
        std::mt19937 rng(static_cast<uint32_t>(splitmix(static_cast<uint64_t>(std::llround(is.cx)) * 73856093ULL ^
                                                         static_cast<uint64_t>(std::llround(is.cz)) * 19349663ULL ^ seed)));
        std::uniform_int_distribution<size_t> pick(0, land.size() - 1);
        std::uniform_real_distribution<float> jitter(0.0f, 1.0f);
        const std::vector<BrushCell> brush = makeBrush(s.brushRadius);
        const int droplets = static_cast<int>(static_cast<float>(land.size()) * s.dropletsPerCell);
        const float seaLevel = 0.3f / cell;   // a hair above 0 m, in cells
        const float maxStep = s.maxChange / cell;

        for (int d = 0; d < droplets; ++d) {
            int start = land[pick(rng)];
            float px = static_cast<float>(start % n) + jitter(rng);
            float pz = static_cast<float>(start / n) + jitter(rng);
            float dirX = 0.0f, dirZ = 0.0f, speed = 1.0f, water = 1.0f, sediment = 0.0f;

            for (int step = 0; step < s.maxSteps; ++step) {
                const int ci = static_cast<int>(px), cj = static_cast<int>(pz);
                float height, gx, gz;
                f.sample(px, pz, height, gx, gz);

                // Turn toward downhill, keeping a little of the old direction.
                dirX = dirX * s.inertia - gx * (1.0f - s.inertia);
                dirZ = dirZ * s.inertia - gz * (1.0f - s.inertia);
                float len = std::sqrt(dirX * dirX + dirZ * dirZ);
                if (len < 1e-6f) break;   // perfectly flat: it pools here
                dirX /= len;
                dirZ /= len;
                const float oldX = px, oldZ = pz;
                px += dirX;
                pz += dirZ;
                if (px < s.brushRadius + 1 || pz < s.brushRadius + 1 || px >= n - s.brushRadius - 2 ||
                    pz >= n - s.brushRadius - 2)
                    break;

                visits[static_cast<size_t>(cj) * n + ci] += water;
                float newH, ngx, ngz;
                f.sample(px, pz, newH, ngx, ngz);
                const float dh = newH - height;   // negative going downhill

                // Reached the sea: a little of the load builds the beach or
                // delta where the stream meets it; the rest washes out to sea.
                if (newH < seaLevel) {
                    for (const BrushCell& b : brush) f.at(ci + b.di, cj + b.dj) += sediment * s.shoreDeposit * b.w;
                    break;
                }

                const float capacity = std::max(-dh, s.minSlope) * speed * water * s.capacity;
                if (sediment > capacity || dh > 0.0f) {
                    // Going uphill: fill the dip behind (never above it).
                    // Too much load: drop some of it.
                    float amount = dh > 0.0f ? std::min(dh, sediment) : (sediment - capacity) * s.depositRate;
                    sediment -= amount;
                    f.deposit(oldX, oldZ, amount);
                } else {
                    // Room for more: wear the ground away with a soft brush,
                    // never digging deeper than the drop to the next cell.
                    float amount = std::min((capacity - sediment) * s.erodeRate, std::min(-dh, maxStep));
                    for (const BrushCell& b : brush) f.at(ci + b.di, cj + b.dj) -= amount * b.w;
                    sediment += amount;
                }

                speed = std::sqrt(std::max(0.0f, speed * speed - dh * s.gravity));
                water *= 1.0f - s.evaporation;
            }
        }
    }

    // ---- 3. keep the change, softened and faded at the edges ------------------
    std::vector<float> raw(f.h.size());
    for (size_t k = 0; k < raw.size(); ++k) raw[k] = f.h[k] * cell - h0[k];
    m.dh.assign(raw.size(), 0.0f);
    m.flow.assign(raw.size(), 0.0f);
    const float edgeCells = static_cast<float>(n) * 0.08f;
    for (int j = 1; j < n - 1; ++j)
        for (int i = 1; i < n - 1; ++i) {
            // A light 3x3 blur takes the speckle out of single droplet paths.
            float sum = 0.0f;
            for (int dj = -1; dj <= 1; ++dj)
                for (int di = -1; di <= 1; ++di)
                    sum += raw[static_cast<size_t>(j + dj) * n + (i + di)] * (di == 0 && dj == 0 ? 4.0f : (di == 0 || dj == 0) ? 2.0f : 1.0f);
            float v = clampf(sum / 16.0f, -s.maxChange, s.maxChange);
            const size_t k = static_cast<size_t>(j) * n + i;
            float edge = static_cast<float>(std::min(std::min(i, j), std::min(n - 1 - i, n - 1 - j)));
            // Sheer ground (sea cliffs) is shaped by waves, not by rain running
            // down it: leave faces steeper than ~55 degrees as they are.
            const float sx = (h0[k + 1] - h0[k - 1]) / (2.0f * cell);
            const float sz = (h0[k + static_cast<size_t>(n)] - h0[k - static_cast<size_t>(n)]) / (2.0f * cell);
            const float slope = std::sqrt(sx * sx + sz * sz);
            float fade = smoothstepf(0.0f, edgeCells, edge) * smoothstepf(-15.0f, -3.0f, h0[k])
                       * (1.0f - smoothstepf(0.9f, 1.6f, slope));
            m.dh[k] = v * fade;
            m.flow[k] = (1.0f - std::exp(-visits[k] / 45.0f)) * fade;   // only real channels count
        }
    return m;
}

} // namespace sea
