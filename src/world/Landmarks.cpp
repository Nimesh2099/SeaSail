#include "Landmarks.h"

#include <algorithm>
#include <cmath>

namespace sea {

namespace {

float rand01(int64_t cx, int64_t cz, uint32_t seed, uint32_t salt)
{
    uint32_t h = static_cast<uint32_t>(cx) * 0x9E3779B1u ^ static_cast<uint32_t>(cz) * 0x85EBCA77u
               ^ static_cast<uint32_t>(cx >> 32) * 0xC2B2AE3Du ^ static_cast<uint32_t>(cz >> 32) * 0x27D4EB2Fu
               ^ seed * 0x165667B1u ^ salt * 0xD3A2646Cu;
    h ^= h >> 15; h *= 0x2c1b3c6dU; h ^= h >> 12; h *= 0x297a2d39U; h ^= h >> 15;
    return static_cast<float>(h >> 8) * (1.0f / 16777216.0f);
}

// Walk out from the island centre along `angle` and return the distance
// where the ground first drops below `level` for good.
double findCoast(const IslandGenerator& w, const IslandInfo& is, float angle, float level)
{
    double dx = std::cos(angle), dz = std::sin(angle);
    double last = 0.0;
    for (double r = 0.0; r < is.radius * 1.4; r += 4.0)
        if (w.height(is.cx + dx * r, is.cz + dz * r) > level) last = r;
    return last;
}

} // namespace

void Landmarks::generate(const IslandGenerator& w, const IslandInfo& is, int64_t cx, int64_t cz,
                         std::vector<Landmark>& out) const
{
    const uint32_t seed = w.seed();

    // Lighthouse: on a headland of bigger islands, set back a little from
    // the edge of the land.
    if ((is.type == IslandType::Mountain && is.radius > 300.0f && rand01(cx, cz, seed, 1) < 0.75f) ||
        (is.type == IslandType::SeaStacks && rand01(cx, cz, seed, 1) < 0.3f)) {
        float angle = rand01(cx, cz, seed, 2) * 2.0f * kPi;
        double coast = findCoast(w, is, angle, 1.5f);
        if (coast > 20.0) {
            double r = coast - 14.0;
            double x = is.cx + std::cos(angle) * r, z = is.cz + std::sin(angle) * r;
            float y = w.height(x, z);
            if (y > 2.0f) out.push_back({LandmarkKind::Lighthouse, x, z, y, angle, rand01(cx, cz, seed, 3) * 6.28f});
        }
    }

    // Jetty: on sandy coasts, a pier running out from the beach with a
    // fishing hut at the land end.
    bool beachy = (is.type == IslandType::Mountain && is.cliffs == 0.0f) || is.type == IslandType::Cay;
    if (beachy && rand01(cx, cz, seed, 4) < 0.7f) {
        // Try a few directions for a gentle beach (not a cliff, not a lagoon).
        for (int attempt = 0; attempt < 6; ++attempt) {
            float angle = rand01(cx, cz, seed, 10 + attempt) * 2.0f * kPi;
            double land = findCoast(w, is, angle, 1.2f);
            if (land < 15.0) continue;
            double dx = std::cos(angle), dz = std::sin(angle);
            double sx = is.cx + dx * land, sz = is.cz + dz * land;
            // Must reach reasonably deep water 30 m out, and the beach must be gentle.
            float endDepth = w.height(sx + dx * 30.0, sz + dz * 30.0);
            float inland = w.height(sx - dx * 12.0, sz - dz * 12.0);
            if (endDepth > -1.5f || inland > 8.0f) continue;
            out.push_back({LandmarkKind::Jetty, sx, sz, 1.6f, angle, 0.0f});
            double hx = sx - dx * 9.0, hz = sz - dz * 9.0;
            out.push_back({LandmarkKind::Hut, hx, hz, w.height(hx, hz), angle, 0.0f});
            break;
        }
    }
}

void Landmarks::update(const IslandGenerator& w, double camX, double camZ, int radiusCells)
{
    const double cs = IslandGenerator::kCellSize;
    const int64_t ccx = static_cast<int64_t>(std::floor(camX / cs));
    const int64_t ccz = static_cast<int64_t>(std::floor(camZ / cs));
    if (ccx == lastCx_ && ccz == lastCz_) return;
    lastCx_ = ccx;
    lastCz_ = ccz;

    active_.clear();
    for (int64_t dz = -radiusCells; dz <= radiusCells; ++dz)
        for (int64_t dx = -radiusCells; dx <= radiusCells; ++dx) {
            auto key = std::make_pair(ccx + dx, ccz + dz);
            auto it = cache_.find(key);
            if (it == cache_.end()) {
                std::vector<Landmark> list;
                IslandInfo is = w.islandInCell(key.first, key.second);
                if (is.type != IslandType::None) generate(w, is, key.first, key.second, list);
                it = cache_.emplace(key, std::move(list)).first;
            }
            active_.insert(active_.end(), it->second.begin(), it->second.end());
        }

    // Forget islands far behind us.
    for (auto it = cache_.begin(); it != cache_.end();) {
        int64_t ddx = it->first.first - ccx, ddz = it->first.second - ccz;
        if (ddx > radiusCells + 2 || -ddx > radiusCells + 2 || ddz > radiusCells + 2 || -ddz > radiusCells + 2)
            it = cache_.erase(it);
        else
            ++it;
    }
}

void Landmarks::gulls(const IslandGenerator& w, double camX, double camZ, double originX, double originZ,
                      double time, std::vector<PropInstance>& out) const
{
    out.clear();
    const double cs = IslandGenerator::kCellSize;
    const int64_t ccx = static_cast<int64_t>(std::floor(camX / cs));
    const int64_t ccz = static_cast<int64_t>(std::floor(camZ / cs));
    for (int64_t dz = -1; dz <= 1; ++dz)
        for (int64_t dx = -1; dx <= 1; ++dx) {
            IslandInfo is = w.islandInCell(ccx + dx, ccz + dz);
            if (is.type == IslandType::None || is.type == IslandType::Cay) continue;
            // A small flock wheeling over each island, each bird on its own
            // circle, height and speed.
            for (int g = 0; g < 7; ++g) {
                float r1 = rand01(ccx + dx, ccz + dz, w.seed(), 100 + g);
                float r2 = rand01(ccx + dx, ccz + dz, w.seed(), 200 + g);
                double radius = 40.0 + 90.0 * r1;
                double speed = (0.18 + 0.12 * r2) * (g % 2 ? 1.0 : -1.0);   // rad/s, some clockwise
                double a = std::fmod(time * speed + r2 * 6.28, 6.283185307);
                double cx = is.cx + std::cos(r1 * 6.28) * is.radius * 0.25;
                double cz = is.cz + std::sin(r1 * 6.28) * is.radius * 0.25;
                double x = cx + std::cos(a) * radius, z = cz + std::sin(a) * radius;
                float y = std::max(30.0f, is.peak * 0.6f) + 25.0f * r2 + 3.0f * static_cast<float>(std::sin(time * 0.7 + g));
                // Facing along the circle (tangent direction).
                float heading = static_cast<float>(a) + (speed > 0 ? kPi * 0.5f : -kPi * 0.5f);
                PropInstance p{static_cast<float>(x - originX), y, static_cast<float>(z - originZ), 1.0f,
                               -heading, 0.9f + 0.2f * r1, static_cast<float>(g) * 1.3f, 0.0f};
                out.push_back(p);
            }
        }
}

} // namespace sea
