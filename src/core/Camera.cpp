#include "Camera.h"

namespace sea {

Mat4 Camera::viewMatrix() const
{
    return orientation.conjugate().toMat4() * Mat4::translation(-position);
}

// Signed angle that rotates `from` onto `to` about `axis` (all unit vectors,
// `from` and `to` already perpendicular to `axis`).
static float signedAngle(const Vec3& from, const Vec3& to, const Vec3& axis)
{
    return std::atan2(dot(cross(from, to), axis), dot(from, to));
}

void Camera::lookAt(const Vec3& target, const Vec3& upHint)
{
    Vec3 f = normalize(target - position);

    // Step 1: shortest rotation taking the default view direction onto f.
    Quat q = Quat::fromTo({0, 0, -1}, f);

    // Step 2: twist about f so the camera's up matches upHint projected
    // onto the plane perpendicular to f.
    Vec3 wantUp = upHint - f * dot(upHint, f);
    if (length(wantUp) > 1e-5f) {
        Vec3 curUp = q.rotate({0, 1, 0});
        float angle = signedAngle(curUp, normalize(wantUp), f);
        q = Quat::fromAxisAngle(f, angle) * q;
    }
    orientation = q.normalized();
}

void Camera::fly(const FlightInput& in, float speed, float dt)
{
    // Mouse yaw/pitch and key roll are rotations about the camera's *own*
    // axes, so they are applied on the right-hand side: q' = q * delta.
    Quat yaw   = Quat::fromAxisAngle({0, 1, 0}, -in.yaw);
    Quat pitch = Quat::fromAxisAngle({1, 0, 0}, -in.pitch);
    Quat roll  = Quat::fromAxisAngle({0, 0, 1}, -in.roll * dt);
    orientation = (orientation * yaw * pitch * roll).normalized();

    Vec3 v = right() * in.move.x + up() * in.move.y + forward() * in.move.z;
    if (length(v) > 1.0f) v = normalize(v); // no faster on diagonals
    position += v * (speed * dt);
}

void Camera::levelTowards(const Vec3& worldUp, float amount)
{
    Vec3 f = forward();
    Vec3 wantUp = worldUp - f * dot(worldUp, f);
    if (length(wantUp) < 1e-4f) return; // looking straight up/down: roll undefined
    float angle = signedAngle(up(), normalize(wantUp), f);
    orientation = (Quat::fromAxisAngle(f, angle * clampf(amount, 0.0f, 1.0f)) * orientation).normalized();
}

} // namespace sea
