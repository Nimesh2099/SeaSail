// ChaseCamera.h - a Dredge-style third-person camera.
//
// It hangs above and behind the boat at a fixed distance, orbits freely
// with the mouse (it does not swing round with the boat), zooms with the
// wheel, and follows the boat with a little smoothing so movement feels
// weighty rather than glued on.
#pragma once

#include "Camera.h"

namespace sea {

class ChaseCamera {
public:
    float yaw      = 0.0f;   // radians, world space, angle of the camera around the boat
    float pitch    = 0.6f;   // radians above the horizon (Dredge sits high: ~35 degrees)
    float distance = 17.0f;  // metres from the focus point

    void snapBehind(float boatHeading) { yaw = boatHeading + kPi; }

    // mouseDx/Dy in pixels, scroll in wheel notches. `focus` is the boat
    // position in camera-relative world space. minY keeps the camera above
    // the water and ground directly below it.
    void update(float mouseDx, float mouseDy, float scroll, const Vec3& focus, float dt);

    // Writes position + orientation into a Camera for rendering.
    void apply(Camera& cam, float minY) const;

    // Where the camera would be (before the minY clamp).
    Vec3 eye() const;

    // The floating origin moved by (dx, dz): move our smoothed state with it.
    void shiftOrigin(float dx, float dz) { smoothFocus_.x -= dx; smoothFocus_.z -= dz; }
    void reset() { initialised_ = false; }

private:
    Vec3 smoothFocus_{};
    bool initialised_ = false;
};

} // namespace sea
