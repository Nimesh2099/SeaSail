#include "ChaseCamera.h"

#include <algorithm>
#include <cmath>

namespace sea {

void ChaseCamera::update(float mouseDx, float mouseDy, float scroll, const Vec3& focus, float dt)
{
    const float sens = 0.0035f;
    yaw   = wrapAngle(yaw + mouseDx * sens);
    pitch = clampf(pitch + mouseDy * sens, 0.12f, 1.35f);
    // Zoom by a fixed ratio per notch, so it feels the same near and far.
    distance = clampf(distance * std::pow(0.88f, scroll), 7.0f, 60.0f);

    // Exponential smoothing of the follow point: the camera trails the boat
    // slightly when it accelerates and catches up when it slows.
    if (!initialised_) {
        smoothFocus_ = focus;
        initialised_ = true;
    }
    float k = 1.0f - std::exp(-7.0f * dt);
    smoothFocus_ = lerp(smoothFocus_, focus, k);
}

Vec3 ChaseCamera::eye() const
{
    Vec3 target = smoothFocus_ + Vec3{0.0f, 1.6f, 0.0f};
    Vec3 dir{std::cos(pitch) * std::cos(yaw), std::sin(pitch), std::cos(pitch) * std::sin(yaw)};
    return target + dir * distance;
}

void ChaseCamera::apply(Camera& cam, float minY) const
{
    Vec3 target = smoothFocus_ + Vec3{0.0f, 1.6f, 0.0f};
    Vec3 p = eye();
    p.y = std::max(p.y, minY);
    cam.position = p;
    cam.lookAt(target, {0, 1, 0});
}

} // namespace sea
