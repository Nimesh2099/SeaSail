// Camera.h - quaternion flight camera.
//
// Orientation is stored as a single unit quaternion, so there is no gimbal
// lock and the camera can fly all the way around the planet: "up" is
// whatever the planet says it is, not a fixed world axis.
#pragma once

#include "SeaMath.h"

namespace sea {

struct FlightInput {
    Vec3  move;          // local axes: x = right, y = up, z = forward  (-1..1)
    float yaw   = 0.0f;  // radians this frame (mouse)
    float pitch = 0.0f;
    float roll  = 0.0f;  // radians per second (keys)
};

class Camera {
public:
    Vec3  position{0, 0, 3};
    Quat  orientation{};
    float fovY = radians(60.0f);

    // Local axes in world space. The camera looks down its local -Z.
    Vec3 forward() const { return orientation.rotate({0, 0, -1}); }
    Vec3 right()   const { return orientation.rotate({1, 0, 0}); }
    Vec3 up()      const { return orientation.rotate({0, 1, 0}); }

    // View matrix = inverse of the camera's world transform.
    // For a rotation the inverse is the conjugate, for a translation it is
    // the negation:  V = R^-1 * T^-1 = R(q*) * T(-p)
    Mat4 viewMatrix() const;

    // Point the camera at `target`, keeping `upHint` as close to "up" as possible.
    void lookAt(const Vec3& target, const Vec3& upHint);

    // Apply one frame of free-flight input. `speed` is in world units / second.
    void fly(const FlightInput& in, float speed, float dt);

    // Roll the camera (about its forward axis) so that its up vector leans
    // toward `worldUp`. amount = 0 does nothing, 1 snaps level.
    void levelTowards(const Vec3& worldUp, float amount);
};

} // namespace sea
