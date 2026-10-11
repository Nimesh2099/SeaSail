#include "Boat.h"

#include "IslandGenerator.h"
#include "Ocean.h"

#include <algorithm>
#include <cmath>

namespace sea {

namespace {

constexpr float kWakeInterval = 0.2f;    // seconds between wake points
constexpr double kWakeLife    = 12.0;    // seconds a wake point lasts

} // namespace

void Boat::placeAt(double x, double z, float heading)
{
    x_ = x;
    z_ = z;
    heading_ = heading;
    velX_ = velZ_ = speedFwd_ = yawRate_ = engine_ = 0.0f;
    y_ = vy_ = pitch_ = roll_ = 0.0f;
    wake_.clear();
}

void Boat::setHeading(float radians)
{
    heading_ = wrapAngle(radians);
    yawRate_ = 0.0f;
    setSpeed(speedFwd_);
}

void Boat::setSpeed(float v)
{
    Vec3 f = forward();
    velX_ = f.x * v;
    velZ_ = f.z * v;
    speedFwd_ = lastSpeed_ = v;
}

Quat Boat::orientation() const
{
    // Boat space has +X forward. Yawing by -heading about +Y turns +X onto
    // (cos h, 0, sin h). Then pitch (about the boat's Z) and roll (about X).
    Quat yaw   = Quat::fromAxisAngle({0, 1, 0}, -heading_);
    Quat pitch = Quat::fromAxisAngle({0, 0, 1}, pitch_);
    Quat roll  = Quat::fromAxisAngle({1, 0, 0}, roll_);
    return (yaw * pitch * roll).normalized();
}

Mat4 Boat::transform(double originX, double originZ) const
{
    return makeTransform({static_cast<float>(x_ - originX), y_, static_cast<float>(z_ - originZ)}, orientation());
}

void Boat::update(const BoatInput& in, const Ocean& ocean, const IslandGenerator& world, double time, float dt)
{
    if (dt <= 0.0f) return;
    const BoatTuning& t = tuning;

    // ---- engine and steering -------------------------------------------------
    engine_ = approach(engine_, clampf(in.throttle, -1.0f, 1.0f), t.engineSpool, dt);

    Vec3 fwd = forward();
    Vec3 side{-fwd.z, 0.0f, fwd.x};  // starboard

    // Split velocity into "along the hull" and "sideways".
    float vf = velX_ * fwd.x + velZ_ * fwd.z;
    float vs = velX_ * side.x + velZ_ * side.z;

    float thrust = engine_ * t.thrust * (engine_ < 0.0f ? t.reverseMul : 1.0f) * (in.boost && engine_ > 0.0f ? t.boostMul : 1.0f);
    float accel  = thrust - t.linearDrag * vf - t.quadDrag * vf * std::fabs(vf);
    vf += accel * dt;
    vs *= std::exp(-t.sideGrip * dt);  // the keel resists sliding sideways

    // Turning: much stronger with water flowing past the rudder, but a
    // little even at rest so you can always turn around.
    float flow = clampf(std::fabs(vf) / 6.0f, 0.0f, 1.0f);
    float targetYawRate = clampf(in.steer, -1.0f, 1.0f) * t.turnRate * (t.idleTurn + (1.0f - t.idleTurn) * flow);
    yawRate_ = approach(yawRate_, targetYawRate, 4.0f, dt);
    heading_ = wrapAngle(heading_ + yawRate_ * dt);

    // Rebuild world velocity from the *new* heading: the old forward speed
    // carries round the turn, the sideways part is what makes it drift.
    fwd  = forward();
    side = {-fwd.z, 0.0f, fwd.x};
    velX_ = fwd.x * vf + side.x * vs;
    velZ_ = fwd.z * vf + side.z * vs;
    speedFwd_ = vf;

    x_ += static_cast<double>(velX_) * dt;
    z_ += static_cast<double>(velZ_) * dt;

    if (ghost) hitShore_ = false;
    else       collideWithShore(world, dt);

    // ---- riding the waves ---------------------------------------------------
    // Sample the real surface under the bow, stern and both sides; the hull
    // follows their average height and tilts to match their slope.
    const float halfLen = (dims_.bowX - dims_.sternX) * 0.4f;
    const float halfBeam = dims_.beam * 0.45f;
    auto surf = [&](float along, float across) {
        return ocean.sample(x_ + fwd.x * along + side.x * across, z_ + fwd.z * along + side.z * across, time).height;
    };
    float hBow = surf(halfLen, 0.0f), hStern = surf(-halfLen, 0.0f);
    float hStbd = surf(0.0f, halfBeam), hPort = surf(0.0f, -halfBeam);
    float targetY = (hBow + hStern + hStbd + hPort) * 0.25f;

    // Heavy, slightly under-damped spring: the boat lags the waves a touch.
    vy_ += (28.0f * (targetY - y_) - 7.0f * vy_) * dt;
    y_  += vy_ * dt;

    float wavePitch = std::atan2(hBow - hStern, 2.0f * halfLen);
    float waveRoll  = std::atan2(hStbd - hPort, 2.0f * halfBeam) * -1.0f;

    // Handling poses: bow lifts with speed and acceleration, the hull leans
    // into turns.
    float dv = (vf - lastSpeed_) / dt;
    lastSpeed_ = vf;
    float bowLift = clampf(vf * 0.009f + dv * 0.03f, -0.08f, 0.12f);
    float lean    = clampf(yawRate_ * vf * 0.04f, -0.22f, 0.22f);

    pitch_ = approach(pitch_, wavePitch + bowLift, 3.0f, dt);
    roll_  = approach(roll_, waveRoll + lean, 3.0f, dt);

    // ---- wake -------------------------------------------------------------
    if (time - lastWakeTime_ >= kWakeInterval) {
        lastWakeTime_ = time;
        double sx = x_ + fwd.x * dims_.sternX, sz = z_ + fwd.z * dims_.sternX;
        float strength = clampf(std::fabs(vf) / 7.0f, 0.0f, 1.0f);
        wake_.push_front({sx, sz, time, strength});
    }
    while (!wake_.empty() && time - wake_.back().time > kWakeLife) wake_.pop_back();
}

void Boat::collideWithShore(const IslandGenerator& world, float dt)
{
    hitShore_ = false;
    Vec3 fwd = forward();
    Vec3 side{-fwd.z, 0.0f, fwd.x};
    const float keel = -dims_.draft;

    // Probe points around the waterline outline of the hull.
    struct Probe { float along, across; };
    const Probe probes[] = {
        {dims_.bowX, 0.0f}, {dims_.bowX * 0.6f, 1.0f}, {dims_.bowX * 0.6f, -1.0f},
        {0.0f, 1.35f}, {0.0f, -1.35f}, {dims_.sternX, 1.1f}, {dims_.sternX, -1.1f},
    };
    float worst = 0.0f;
    for (const Probe& p : probes) {
        float h = world.height(x_ + fwd.x * p.along + side.x * p.across, z_ + fwd.z * p.along + side.z * p.across);
        worst = std::max(worst, h - keel);
    }
    if (worst <= 0.0f) return;
    hitShore_ = true;

    // Push out "downhill", toward deeper water: the negative gradient of
    // the sea-floor height around the boat.
    const double e = 3.0;
    float gx = (world.height(x_ + e, z_) - world.height(x_ - e, z_)) / static_cast<float>(2.0 * e);
    float gz = (world.height(x_, z_ + e) - world.height(x_, z_ - e)) / static_cast<float>(2.0 * e);
    float len = std::sqrt(gx * gx + gz * gz);
    Vec3 out = len > 1e-4f ? Vec3{-gx / len, 0.0f, -gz / len} : -fwd;

    // Cancel the velocity heading into the shore, with a small bounce.
    float into = velX_ * out.x + velZ_ * out.z;
    if (into < 0.0f) {
        velX_ -= 1.3f * into * out.x;
        velZ_ -= 1.3f * into * out.z;
    }
    // Scrape: grinding on the bottom slows everything down.
    float scrape = std::exp(-3.0f * dt);
    velX_ *= scrape;
    velZ_ *= scrape;
    speedFwd_ = velX_ * fwd.x + velZ_ * fwd.z;

    float push = std::min(worst, 1.0f) * 4.0f * dt + 0.02f;
    x_ += static_cast<double>(out.x * push);
    z_ += static_cast<double>(out.z * push);
}

} // namespace sea
