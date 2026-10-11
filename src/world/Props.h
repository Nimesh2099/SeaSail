// Props.h - things that grow and lie on the islands: palms, broadleaf
// trees, pines, bushes and rocks.
//
// Placement is deterministic: a fixed 4 m world lattice, one candidate per
// lattice cell, and a hash of the cell decides jitter, type, size and
// rotation. So the same tree is always in the same place, whichever thread
// or LOD generated it. Each candidate is then accepted or rejected by
// rules on height, slope and moisture.
#pragma once

#include "SeaMath.h"

#include <cstdint>
#include <vector>

namespace sea {

// Gulls aren't scattered on the ground, but they're drawn the same way.
enum class PropType : int { Palm, Broadleaf, Pine, Bush, Rock, Gull, Count };
constexpr int kPropTypes = static_cast<int>(PropType::Count);

struct PropInstance {
    float x, y, z;   // position (chunk-local when stored, camera-relative when drawn)
    float scale;
    float yaw;       // radians about +Y
    float tint;      // colour multiplier, ~0.8..1.2
    float phase;     // wind-sway phase
    float pad;
};

using PropLists = std::vector<PropInstance>[kPropTypes];

// The chunk's height grid, as built for its mesh, so props sit exactly on
// the rendered triangles. Index (i, j) in [-1, n+1] (one-sample border).
struct HeightGridView {
    const float* height;
    const float* moisture;
    int   gridSize;   // n + 3
    int   cells;      // n
    float step;       // metres between samples

    float h(int i, int j) const { return height[(j + 1) * gridSize + (i + 1)]; }
    float m(int i, int j) const { return moisture[(j + 1) * gridSize + (i + 1)]; }

    // Height on the actual mesh triangles at chunk-local (x, z).
    float heightAt(float x, float z, Vec3* normal) const;
    float moistureAt(float x, float z) const;
};

// Fills `out` with the props of one chunk whose corner is at world
// (worldX0, worldZ0) and that is `chunkSize` metres across.
void scatterProps(const HeightGridView& grid, double worldX0, double worldZ0, double chunkSize,
                  uint32_t seed, PropLists& out);

} // namespace sea
