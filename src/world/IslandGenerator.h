// IslandGenerator.h - height of the sea floor / land at any point on an
// endless ocean.
//
// The world is cut into square cells. A hash of each cell's coordinates
// (and the seed) decides whether it holds an island, what kind, where, and
// how big. Nothing is stored, so the world is endless and costs no memory,
// and any point can be evaluated on any thread in any order.
#pragma once

#include "Noise.h"

#include <cstdint>

namespace sea {

enum class IslandType : uint8_t {
    None,
    Mountain,   // big volcanic / hilly island, beaches or cliffs
    Atoll,      // ring of sand around a shallow turquoise lagoon
    SeaStacks,  // cluster of steep rock pillars on a shallow shelf
    Cay,        // small, flat, sandy islet
};

struct IslandInfo {
    IslandType type = IslandType::None;
    double cx = 0, cz = 0;  // centre, world metres
    float  radius = 0;      // reach of the island's underwater base
    float  peak = 0;        // rough summit height
    float  cliffs = 0;      // 0 = sandy beaches, 1 = sheer coastal cliffs
};

struct TerrainSample {
    float height;    // metres; sea level is 0
    float moisture;  // 0..1, lush vs dry vegetation
};

class IslandGenerator {
public:
    static constexpr double kCellSize = 1500.0;  // at most one island per cell

    explicit IslandGenerator(uint32_t seed);

    TerrainSample sample(double x, double z) const;
    float height(double x, double z) const { return sample(x, z).height; }

    IslandInfo islandInCell(int64_t cx, int64_t cz) const;

    // Nearest island of the given type (or any type if None) to (x, z).
    IslandInfo nearestIsland(double x, double z, IslandType wanted = IslandType::None, int maxRings = 8) const;

    uint32_t seed() const { return seed_; }

private:
    uint32_t      seed_;
    GradientNoise noise_;
};

} // namespace sea
