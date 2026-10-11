#include "Environment.h"

#include <cmath>

namespace sea {

void Environment::update(float dt, float scrub)
{
    if (!paused) hours += dt * 24.0f / (dayLengthMinutes * 60.0f);
    hours += scrub * dt;
    hours = std::fmod(hours, 24.0f);
    if (hours < 0.0f) hours += 24.0f;

    // Clouds drift with a steady breeze. Wrapping at 4096 m is invisible
    // because the cloud noise repeats exactly every 4096 m.
    cloudX = std::fmod(cloudX + 6.0 * dt, 4096.0);
    cloudZ = std::fmod(cloudZ + 2.5 * dt, 4096.0);
}

Vec3 Environment::sunDir() const
{
    float theta = (hours - 6.0f) / 24.0f * 2.0f * kPi;  // 0 at sunrise
    const Vec3 east{1.0f, 0.0f, 0.0f};
    const Vec3 noon = normalize(Vec3{0.0f, 0.82f, 0.57f}); // up, tilted south
    return normalize(east * std::cos(theta) + noon * std::sin(theta));
}

Vec3 Environment::moonDir() const
{
    // Roughly opposite the sun, offset so it's not a mirror image.
    Vec3 s = sunDir();
    return normalize(Vec3{-s.x * 0.9f + 0.25f, -s.y + 0.15f, -s.z * 0.9f - 0.2f});
}

float Environment::night() const
{
    return 1.0f - smoothstepf(-0.12f, 0.08f, sunDir().y);
}

Vec3 Environment::sunColor() const
{
    float high = smoothstepf(0.0f, 0.35f, sunDir().y); // reddens near the horizon
    return Vec3{1.0f, 0.45f, 0.18f} * (1.0f - high) + Vec3{1.0f, 0.96f, 0.90f} * high;
}

float Environment::sunIntensity() const
{
    return 2.7f * smoothstepf(-0.06f, 0.12f, sunDir().y);
}

Vec3 Environment::moonLight() const
{
    float up = smoothstepf(-0.05f, 0.25f, moonDir().y);
    return Vec3{0.50f, 0.60f, 0.85f} * (0.30f * up * night());
}

float Environment::exposure() const
{
    return lerpf(0.6f, 1.4f, night());
}

} // namespace sea
