// SeaMath.h - small hand-written 3D math library (no GLM).
// (Not "Math.h": on Windows file names ignore case, and it would shadow <math.h>.)
//
// Conventions
//   * Right-handed world space, +Y up. Camera looks down its local -Z.
//   * Mat4 is column-major (m[col * 4 + row]) so it can be handed straight
//     to glUniformMatrix4fv with transpose = GL_FALSE.
//   * Quat is (w, x, y, z), unit length when used as a rotation.
//     a * b means "rotate by b, then by a" - the same order as matrices.
#pragma once

#include <cmath>

namespace sea {

constexpr float kPi = 3.14159265358979323846f;

inline float radians(float deg) { return deg * (kPi / 180.0f); }
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline float smoothstepf(float e0, float e1, float x)
{
    float t = clampf((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// ---------------------------------------------------------------- Vec3 ----
struct Vec3 {
    float x = 0, y = 0, z = 0;

    constexpr Vec3() = default;
    constexpr Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    Vec3 operator/(float s) const { return {x / s, y / s, z / s}; }
    Vec3 operator-() const { return {-x, -y, -z}; }
    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    Vec3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
};

inline Vec3 operator*(float s, const Vec3& v) { return v * s; }

inline float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

// |a x b| = |a||b|sin(theta), direction by the right-hand rule.
inline Vec3 cross(const Vec3& a, const Vec3& b)
{
    return {a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x};
}

inline float length(const Vec3& v) { return std::sqrt(dot(v, v)); }

inline Vec3 normalize(const Vec3& v)
{
    float len = length(v);
    return len > 1e-12f ? v / len : Vec3{0, 0, 0};
}

// Any unit vector perpendicular to n (n must be unit length).
inline Vec3 anyPerpendicular(const Vec3& n)
{
    // Cross with whichever world axis is least parallel to n.
    Vec3 axis = std::fabs(n.x) < 0.9f ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
    return normalize(cross(n, axis));
}

// ---------------------------------------------------------------- Mat4 ----
struct Mat4 {
    float m[16];

    static Mat4 identity()
    {
        Mat4 r{};
        r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
        return r;
    }

    float& at(int row, int col) { return m[col * 4 + row]; }
    float at(int row, int col) const { return m[col * 4 + row]; }

    // (A * B) applied to v == A * (B * v)
    Mat4 operator*(const Mat4& b) const
    {
        Mat4 r{};
        for (int col = 0; col < 4; ++col)
            for (int row = 0; row < 4; ++row) {
                float s = 0.0f;
                for (int k = 0; k < 4; ++k) s += at(row, k) * b.at(k, col);
                r.at(row, col) = s;
            }
        return r;
    }

    Vec3 transformPoint(const Vec3& p) const
    {
        float x = at(0, 0) * p.x + at(0, 1) * p.y + at(0, 2) * p.z + at(0, 3);
        float y = at(1, 0) * p.x + at(1, 1) * p.y + at(1, 2) * p.z + at(1, 3);
        float z = at(2, 0) * p.x + at(2, 1) * p.y + at(2, 2) * p.z + at(2, 3);
        float w = at(3, 0) * p.x + at(3, 1) * p.y + at(3, 2) * p.z + at(3, 3);
        return w != 0.0f ? Vec3{x / w, y / w, z / w} : Vec3{x, y, z};
    }

    static Mat4 translation(const Vec3& t)
    {
        Mat4 r = identity();
        r.at(0, 3) = t.x;
        r.at(1, 3) = t.y;
        r.at(2, 3) = t.z;
        return r;
    }

    static Mat4 scale(float s)
    {
        Mat4 r = identity();
        r.at(0, 0) = r.at(1, 1) = r.at(2, 2) = s;
        return r;
    }

    // OpenGL-style perspective: right-handed view space, clip z in [-w, w].
    static Mat4 perspective(float fovyRadians, float aspect, float zNear, float zFar)
    {
        float f = 1.0f / std::tan(fovyRadians * 0.5f);
        Mat4 r{};
        r.at(0, 0) = f / aspect;
        r.at(1, 1) = f;
        r.at(2, 2) = (zFar + zNear) / (zNear - zFar);
        r.at(2, 3) = (2.0f * zFar * zNear) / (zNear - zFar);
        r.at(3, 2) = -1.0f;
        return r;
    }
};

// ---------------------------------------------------------------- Quat ----
struct Quat {
    float w = 1, x = 0, y = 0, z = 0;

    static Quat identity() { return {}; }

    // Rotation of `angle` radians about unit vector `axis`:
    //   q = (cos(a/2), axis * sin(a/2))
    static Quat fromAxisAngle(const Vec3& axis, float angle)
    {
        Vec3 n = normalize(axis);
        float s = std::sin(angle * 0.5f);
        return {std::cos(angle * 0.5f), n.x * s, n.y * s, n.z * s};
    }

    // Hamilton product. (a * b) rotates by b first, then by a.
    Quat operator*(const Quat& b) const
    {
        return {w * b.w - x * b.x - y * b.y - z * b.z,
                w * b.x + x * b.w + y * b.z - z * b.y,
                w * b.y - x * b.z + y * b.w + z * b.x,
                w * b.z + x * b.y - y * b.x + z * b.w};
    }

    Quat conjugate() const { return {w, -x, -y, -z}; }

    Quat normalized() const
    {
        float len = std::sqrt(w * w + x * x + y * y + z * z);
        return {w / len, x / len, y / len, z / len};
    }

    // v' = q v q*, expanded so it costs two cross products instead of
    // two full quaternion multiplies:
    //   v' = v + 2w(u x v) + 2u x (u x v),   u = (x, y, z)
    Vec3 rotate(const Vec3& v) const
    {
        Vec3 u{x, y, z};
        Vec3 t = cross(u, v) * 2.0f;
        return v + t * w + cross(u, t);
    }

    Mat4 toMat4() const
    {
        Mat4 r = Mat4::identity();
        float xx = x * x, yy = y * y, zz = z * z;
        float xy = x * y, xz = x * z, yz = y * z;
        float wx = w * x, wy = w * y, wz = w * z;
        r.at(0, 0) = 1 - 2 * (yy + zz); r.at(0, 1) = 2 * (xy - wz);     r.at(0, 2) = 2 * (xz + wy);
        r.at(1, 0) = 2 * (xy + wz);     r.at(1, 1) = 1 - 2 * (xx + zz); r.at(1, 2) = 2 * (yz - wx);
        r.at(2, 0) = 2 * (xz - wy);     r.at(2, 1) = 2 * (yz + wx);     r.at(2, 2) = 1 - 2 * (xx + yy);
        return r;
    }

    // Shortest-arc rotation taking unit vector `from` onto unit vector `to`.
    static Quat fromTo(const Vec3& from, const Vec3& to)
    {
        float d = dot(from, to);
        if (d < -0.999999f) // opposite: rotate 180 degrees about any perpendicular axis
            return fromAxisAngle(anyPerpendicular(from), kPi);
        Vec3 c = cross(from, to);
        return Quat{1.0f + d, c.x, c.y, c.z}.normalized();
    }
};

// ------------------------------------------------------------- helpers ---

// Rigid transform: rotate by q, then move to t.  M = T(t) * R(q)
inline Mat4 makeTransform(const Vec3& t, const Quat& q)
{
    Mat4 m = q.toMat4();
    m.at(0, 3) = t.x;
    m.at(1, 3) = t.y;
    m.at(2, 3) = t.z;
    return m;
}

inline Vec3 lerp(const Vec3& a, const Vec3& b, float t) { return a + (b - a) * t; }

// Wrap an angle to (-pi, pi].
inline float wrapAngle(float a)
{
    while (a > kPi) a -= 2.0f * kPi;
    while (a <= -kPi) a += 2.0f * kPi;
    return a;
}

// Frame-rate independent exponential approach: after `dt` seconds, `current`
// has moved toward `target` as if pulled with time constant 1/rate.
inline float approach(float current, float target, float rate, float dt)
{
    return target + (current - target) * std::exp(-rate * dt);
}

} // namespace sea
