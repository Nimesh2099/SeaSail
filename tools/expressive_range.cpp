// expressive_range.cpp - measures many generated islands so the generator's
// output space can be plotted (expressive range analysis, after Smith &
// Whitehead, "Analyzing the Expressive Range of a Level Generator", PCG
// Workshop 2010).
//
// For every island in a block of cells, across several seeds, it samples the
// height field on a regular grid and computes a few shape metrics, once with
// erosion and once without. Results go to a CSV file, one row per island and
// erosion setting. No window or GPU needed: the terrain is a pure function.
//
// Usage: ExpressiveRange [--seeds N] [--cells N] [--res METRES] [--out file.csv]
//   --seeds  number of world seeds (default 12)
//   --cells  cells per side scanned in each world (default 8, so 64 cells)
//   --res    sample spacing in metres (default 4)

#include "IslandGenerator.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <queue>
#include <string>
#include <vector>

using namespace sea;

namespace {

constexpr double kPi = 3.14159265358979323846;

const char* typeName(IslandType t) {
    switch (t) {
    case IslandType::Mountain:  return "mountain";
    case IslandType::Atoll:     return "atoll";
    case IslandType::SeaStacks: return "stacks";
    case IslandType::Cay:       return "cay";
    default:                    return "none";
    }
}

struct Metrics {
    double landArea = 0;     // km^2 above sea level
    double peak = 0;         // m
    double meanLandHeight = 0;
    double meanSlope = 0;    // degrees, over land
    double steepShare = 0;   // share of land steeper than 35 degrees
    double ruggedness = 0;   // terrain ruggedness index: mean |h - neighbour| over land, m
    double coastIndex = 0;   // coastline length / circumference of a circle of the same area (1 = circle)
    int    landmasses = 0;   // separate pieces of land bigger than 4 samples
    double ms = 0;           // time to sample (includes erosion on first use)
};

// Points nearer (relative to radius) to a neighbouring island belong to that
// island, so a big neighbour's slopes don't leak into a small island's numbers.
Metrics measure(const IslandGenerator& gen, const IslandInfo& is, int64_t cellX, int64_t cellZ, double res) {
    Metrics m;
    std::vector<IslandInfo> neighbours;
    for (int64_t dz = -1; dz <= 1; ++dz)
        for (int64_t dx = -1; dx <= 1; ++dx) {
            if (!dx && !dz) continue;
            IslandInfo o = gen.islandInCell(cellX + dx, cellZ + dz);
            if (o.type != IslandType::None) neighbours.push_back(o);
        }
    const double r = is.radius;
    const int n = static_cast<int>(std::ceil(2 * r / res)) + 1;
    const double x0 = is.cx - r, z0 = is.cz - r;
    std::vector<float> h(static_cast<size_t>(n) * n, -1000.0f);
    std::vector<char> inside(static_cast<size_t>(n) * n, 0);

    auto t0 = std::chrono::steady_clock::now();
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) {
            double x = x0 + i * res, z = z0 + j * res;
            double dx = x - is.cx, dz = z - is.cz;
            double d2 = dx * dx + dz * dz;
            if (d2 > r * r) continue;   // stay on this island's disc
            double own = std::sqrt(d2) / r;
            bool theirs = false;
            for (const IslandInfo& o : neighbours) {
                double ox = x - o.cx, oz = z - o.cz;
                if (std::sqrt(ox * ox + oz * oz) / o.radius < own) { theirs = true; break; }
            }
            if (theirs) continue;
            inside[static_cast<size_t>(j) * n + i] = 1;
            h[static_cast<size_t>(j) * n + i] = gen.height(x, z);
        }
    m.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();

    auto at = [&](int i, int j) { return h[static_cast<size_t>(j) * n + i]; };
    auto land = [&](int i, int j) {
        return i >= 0 && j >= 0 && i < n && j < n && inside[static_cast<size_t>(j) * n + i] && at(i, j) > 0.0f;
    };

    long landCells = 0, steep = 0;
    double sumH = 0, sumSlope = 0, sumTri = 0, coastEdges = 0;
    for (int j = 1; j < n - 1; ++j)
        for (int i = 1; i < n - 1; ++i) {
            if (!land(i, j)) continue;
            ++landCells;
            float hc = at(i, j);
            sumH += hc;
            m.peak = std::max(m.peak, static_cast<double>(hc));
            double gx = (at(i + 1, j) - at(i - 1, j)) / (2 * res);
            double gz = (at(i, j + 1) - at(i, j - 1)) / (2 * res);
            double slope = std::atan(std::sqrt(gx * gx + gz * gz)) * 57.29578;
            sumSlope += slope;
            if (slope > 35.0) ++steep;
            double tri = 0;
            for (int dj = -1; dj <= 1; ++dj)
                for (int di = -1; di <= 1; ++di)
                    if (di || dj) tri += std::fabs(hc - std::max(at(i + di, j + dj), -50.0f));
            sumTri += tri / 8.0;
            // Coastline: each side of this land cell that faces water.
            if (!land(i + 1, j)) ++coastEdges;
            if (!land(i - 1, j)) ++coastEdges;
            if (!land(i, j + 1)) ++coastEdges;
            if (!land(i, j - 1)) ++coastEdges;
        }
    if (landCells == 0) return m;

    double cellArea = res * res;
    m.landArea = landCells * cellArea / 1e6;
    m.meanLandHeight = sumH / landCells;
    m.meanSlope = sumSlope / landCells;
    m.steepShare = static_cast<double>(steep) / landCells;
    m.ruggedness = sumTri / landCells;
    // Edge counting on a grid overstates a smooth curve's length by about 4/pi.
    double coastLen = coastEdges * res * (kPi / 4.0);
    double circle = 2.0 * std::sqrt(kPi * landCells * cellArea);
    m.coastIndex = coastLen / circle;

    // Landmasses: 4-connected flood fill, ignoring specks of < 5 samples.
    std::vector<char> seen(static_cast<size_t>(n) * n, 0);
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) {
            if (!land(i, j) || seen[static_cast<size_t>(j) * n + i]) continue;
            int count = 0;
            std::queue<std::pair<int, int>> q;
            q.push({i, j});
            seen[static_cast<size_t>(j) * n + i] = 1;
            while (!q.empty()) {
                auto [a, b] = q.front(); q.pop();
                ++count;
                const int d[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
                for (auto& e : d) {
                    int u = a + e[0], v = b + e[1];
                    if (land(u, v) && !seen[static_cast<size_t>(v) * n + u]) {
                        seen[static_cast<size_t>(v) * n + u] = 1;
                        q.push({u, v});
                    }
                }
            }
            if (count >= 5) ++m.landmasses;
        }
    return m;
}

} // namespace

int main(int argc, char** argv) {
    int seeds = 12, cells = 8;
    double res = 4.0;
    std::string out = "expressive_range.csv";
    for (int i = 1; i < argc; ++i) {
        auto next = [&]() { return i + 1 < argc ? argv[++i] : ""; };
        if      (!std::strcmp(argv[i], "--seeds")) seeds = std::atoi(next());
        else if (!std::strcmp(argv[i], "--cells")) cells = std::atoi(next());
        else if (!std::strcmp(argv[i], "--res"))   res   = std::atof(next());
        else if (!std::strcmp(argv[i], "--out"))   out   = next();
    }

    std::FILE* f = std::fopen(out.c_str(), "w");
    if (!f) { std::fprintf(stderr, "Cannot write %s\n", out.c_str()); return 1; }
    std::fprintf(f, "seed,cell_x,cell_z,type,erosion,radius_m,cliffs,land_area_km2,peak_m,mean_land_height_m,"
                    "mean_slope_deg,steep_share,ruggedness_m,coast_index,landmasses,sample_ms\n");

    int islands = 0;
    for (int s = 1; s <= seeds; ++s) {
        IslandGenerator eroded(static_cast<uint32_t>(s), true);
        IslandGenerator raw(static_cast<uint32_t>(s), false);
        for (int cz = -cells / 2; cz < cells - cells / 2; ++cz)
            for (int cx = -cells / 2; cx < cells - cells / 2; ++cx) {
                IslandInfo is = raw.islandInCell(cx, cz);
                if (is.type == IslandType::None) continue;
                ++islands;
                for (int e = 0; e < 2; ++e) {
                    Metrics m = measure(e ? eroded : raw, is, cx, cz, res);
                    std::fprintf(f, "%d,%d,%d,%s,%d,%.1f,%.3f,%.4f,%.2f,%.2f,%.2f,%.4f,%.3f,%.4f,%d,%.1f\n",
                                 s, cx, cz, typeName(is.type), e, is.radius, is.cliffs, m.landArea, m.peak,
                                 m.meanLandHeight, m.meanSlope, m.steepShare, m.ruggedness, m.coastIndex,
                                 m.landmasses, m.ms);
                }
            }
        std::fflush(f);
        std::fprintf(stderr, "seed %d done, %d islands so far\n", s, islands);
    }
    std::fclose(f);
    std::fprintf(stderr, "Wrote %d islands (x2 erosion settings) to %s\n", islands, out.c_str());
    return 0;
}
