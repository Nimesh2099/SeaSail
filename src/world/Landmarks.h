// Landmarks.h - hand-made-looking places on procedural islands:
// lighthouses on headlands, wooden jetties with a fishing hut on beaches,
// and the gulls that circle the islands.
//
// Like everything else they're derived from the seed and the island, so
// the lighthouse on an island is always in the same spot. They're worked out
// once per island, the first time it comes near, and cached.
#pragma once

#include "IslandGenerator.h"
#include "SeaMath.h"
#include "Props.h"

#include <cstdint>
#include <map>
#include <utility>
#include <vector>

namespace sea {

enum class LandmarkKind { Lighthouse, Jetty, Hut };

struct Landmark {
    LandmarkKind kind;
    double x, z;     // world position, metres
    float  y;        // ground (or deck) height
    float  yaw;      // radians; for the jetty, the direction it points out to sea
    float  phase;    // lighthouse beam phase
};

class Landmarks {
public:
    // Make sure every island within `radiusCells` cells of the camera has
    // its landmarks worked out; forget ones far away.
    void update(const IslandGenerator& world, double camX, double camZ, int radiusCells = 2);

    const std::vector<Landmark>& all() const { return active_; }

    // Gulls circling nearby islands, as prop instances (camera-relative).
    void gulls(const IslandGenerator& world, double camX, double camZ, double originX, double originZ,
               double time, std::vector<PropInstance>& out) const;

private:
    void generate(const IslandGenerator& world, const IslandInfo& is, int64_t cx, int64_t cz,
                  std::vector<Landmark>& out) const;

    std::map<std::pair<int64_t, int64_t>, std::vector<Landmark>> cache_;
    std::vector<Landmark> active_;
    int64_t lastCx_ = INT64_MIN, lastCz_ = INT64_MIN;
};

} // namespace sea
