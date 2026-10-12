// Erosion.h - hydraulic erosion of one island, by simulated rain droplets.
//
// The world's base terrain is a pure function of (x, z). Erosion can't be:
// every droplet changes the ground the next one flows over. So each island
// is eroded once, on a grid covering it, the first time anything needs it,
// and the result is kept as a grid of height *changes* that IslandGenerator
// adds to its base terrain (bilinearly interpolated, fading to zero at the
// grid's edge so it joins the untouched sea floor seamlessly).
//
// The droplet model (after Hans Theobald Beyer, 2015):
//   - a droplet starts at a random spot on land with some water and speed 1
//   - each step it moves one cell downhill (with a little inertia), speeds
//     up on steep ground and evaporates a little
//   - its sediment capacity grows with slope, speed and water:
//       capacity = max(-dh, minSlope) * speed * water * capacityFactor
//   - carrying less than it could, it erodes the ground around it (a small
//     brush, so valleys are smooth); carrying more, or going uphill, it
//     drops sediment (fans, deltas, valley floors)
//   - reaching the sea, it leaves part of its load at the shore (beaches,
//     river mouths) and stops; the rest is carried out to sea.
// Many thousands of droplets carve branching gullies and valleys. Where the
// droplets ran is also recorded, so valleys can be greener (moister).
#pragma once

#include "IslandGenerator.h"

#include <functional>
#include <vector>

namespace sea {

struct ErosionMap {
    double x0 = 0, z0 = 0;   // world position of grid point (0, 0)
    double cell = 1;         // metres between grid points
    int    n = 0;            // grid points per side
    std::vector<float> dh;   // height change, metres (n * n)
    std::vector<float> flow; // 0..1, how much water ran here (n * n)

    // Bilinear lookup; false if (x, z) is outside the grid.
    bool lookup(double x, double z, float& outDh, float& outFlow) const;
};

struct ErosionSettings {
    int   gridSize      = 384;    // grid points per side
    float dropletsPerCell = 0.9f; // droplets per land cell
    int   maxSteps      = 80;
    float inertia       = 0.05f;  // 0 = always straight downhill, 1 = never turns
    float capacity      = 6.0f;
    float minSlope      = 0.01f;
    float depositRate   = 0.25f;
    float erodeRate     = 0.3f;
    float evaporation   = 0.015f;
    float gravity       = 4.0f;
    int   brushRadius   = 2;      // cells
    float maxChange     = 24.0f;  // metres: limits how deep any valley gets
    float shoreDeposit  = 0.25f;  // share of a stream's load left at the coast
};

// Erodes the island `is`. `base` gives the uneroded terrain at any point.
ErosionMap erodeIsland(const IslandInfo& is, const std::function<float(double, double)>& base, uint32_t seed,
                       const ErosionSettings& settings = {});

} // namespace sea
