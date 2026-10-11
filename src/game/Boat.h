// Boat.h - arcade boat handling in the spirit of Dredge.
//
// Tank controls: throttle drives the boat along its heading, steering turns
// it (even when stopped, just more slowly). Speed builds up and bleeds off
// with momentum; the hull slides a little sideways in fast turns, leans
// into them, lifts its bow under power, and rides the actual ocean waves.
#pragma once

#include "BoatModel.h"
#include "SeaMath.h"

#include <deque>

namespace sea {

class IslandGenerator;
class Ocean;

struct BoatInput {
    float throttle = 0.0f;  // -1 (reverse) .. +1 (full ahead)
    float steer    = 0.0f;  // -1 (port / left) .. +1 (starboard / right)
    bool  boost    = false;
};

// A point the boat passed through, for the foam wake.
struct WakePoint {
    double x, z;
    double time;
    float  strength;  // 0..1, from speed when it was laid down
};

class Boat {
public:
    void placeAt(double x, double z, float heading);
    void update(const BoatInput& in, const Ocean& ocean, const IslandGenerator& world, double time, float dt);

    // World-space pose. Position is absolute (double); use transform() to get
    // a camera-relative matrix for drawing.
    double x() const { return x_; }
    double z() const { return z_; }
    float  y() const { return y_; }
    float  heading() const { return heading_; }  // radians; forward = (cos, 0, sin)
    float  speed() const { return speedFwd_; }   // m/s along the heading
    float  engine() const { return engine_; }    // smoothed throttle, -1..1
    bool   hitShoreThisFrame() const { return hitShore_; }

    Quat orientation() const;
    Mat4 transform(double originX, double originZ) const;
    Vec3 forward() const { return {std::cos(heading_), 0.0f, std::sin(heading_)}; }

    const std::deque<WakePoint>& wake() const { return wake_; }
    const BoatDimensions& dims() const { return dims_; }

private:
    void collideWithShore(const IslandGenerator& world, float dt);

    BoatDimensions dims_;

    double x_ = 0.0, z_ = 0.0;
    float  y_ = 0.0f, vy_ = 0.0f;       // vertical bob (spring)
    float  heading_ = 0.0f, yawRate_ = 0.0f;
    float  velX_ = 0.0f, velZ_ = 0.0f;  // world-space horizontal velocity
    float  speedFwd_ = 0.0f;
    float  engine_ = 0.0f;
    float  pitch_ = 0.0f, roll_ = 0.0f; // radians: bow up / lean to starboard
    float  lastSpeed_ = 0.0f;
    bool   hitShore_ = false;

    std::deque<WakePoint> wake_;
    double lastWakeTime_ = -1e9;
};

} // namespace sea
