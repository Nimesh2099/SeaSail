// Environment.h - time of day: where the sun and moon are, what colour the
// light is, how dark it is, and which way the clouds drift.
#pragma once

#include "SeaMath.h"

namespace sea {

class Environment {
public:
    float hours = 10.0f;            // time of day, 0..24
    float dayLengthMinutes = 16.0f; // real minutes for a full day
    bool  paused = false;

    void update(float dt, float scrubHoursPerSecond);

    // Sun path: rises in the east (+x) at 06:00, highest (~55 degrees, to the
    // south) at noon, sets in the west at 18:00.
    Vec3  sunDir() const;
    Vec3  moonDir() const;
    float night() const;          // 0 by day .. 1 at night
    Vec3  sunColor() const;
    float sunIntensity() const;
    Vec3  moonLight() const;      // colour * intensity
    float exposure() const;       // eyes adapt: brighter exposure at night

    double cloudX = 0.0, cloudZ = 0.0;  // drift, metres (wrapped to 4096)
};

} // namespace sea
