// BoatModel.h - a small fishing boat, modelled entirely in code.
//
// Boat space: +X is the bow (forward), +Y is up, +Z is starboard (right).
// y = 0 is the waterline.
#pragma once

#include "Mesh.h"

namespace sea {

struct BoatDimensions {
    float sternX  = -3.6f;  // metres
    float bowX    =  4.4f;
    float beam    =  2.8f;  // full width
    float draft   =  0.75f; // keel depth below the waterline
};

// Where the boat's lights are, in boat space (see buildBoatModel).
inline constexpr Vec3 kMastLampPos{0.24f, 5.21f, 0.0f};
inline constexpr Vec3 kDeckLampPos{-1.6f, 2.1f, 0.0f};
inline constexpr Vec3 kSpotlightPos{1.0f, 2.96f, 0.0f};
inline constexpr Vec3 kSpotlightDir{0.987f, -0.16f, 0.0f};  // ahead and slightly down

// Fills `out` with the boat's triangles.
void buildBoatModel(MeshBuilder& out, const BoatDimensions& dim = {});

} // namespace sea
