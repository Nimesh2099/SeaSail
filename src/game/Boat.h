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

// Handling. The console's `tune` command edits these live.
struct BoatTuning {
    float thrust     = 4.2f;    // m/s^2 at full throttle
    float boostMul   = 1.7f;
    float reverseMul = 0.45f;   // reverse is weaker
    float linearDrag = 0.22f;   // 1/s
    float quadDrag   = 0.035f;  // 1/m   -> top speed ~ 8 m/s (15 knots)
    float sideGrip   = 2.8f;    // how fast sideways drift dies out (1/s)
    float turnRate   = 0.95f;   // rad/s at speed
    float idleTurn   = 0.35f;   // fraction of turnRate when stopped
    float engineSpool = 2.5f;   // how fast the throttle responds (1/s)
};

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
    BoatTuning tuning;
    bool ghost = false;  // sail through land (testing)

    void placeAt(double x, double z, float heading);
    // Teleport-free tweaks for testing: turn on the spot, or set the speed
    // along the current heading (sideways drift is cleared).
    void setHeading(float radians);
    void setSpeed(float metresPerSecond);
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
