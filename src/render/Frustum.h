// Frustum.h - view-frustum culling against axis-aligned boxes.
#pragma once

#include "SeaMath.h"

namespace sea {

class Frustum {
public:
    // Gribb & Hartmann: each clip plane is a sum or difference of rows of
    // the view-projection matrix. A point p is inside when, for every plane,
    // dot(plane.xyz, p) + plane.w >= 0.
    explicit Frustum(const Mat4& vp)
    {
        auto row = [&](int r, float out[4]) {
            for (int c = 0; c < 4; ++c) out[c] = vp.at(r, c);
        };
        float r0[4], r1[4], r2[4], r3[4];
        row(0, r0); row(1, r1); row(2, r2); row(3, r3);
        for (int c = 0; c < 4; ++c) {
            p_[0][c] = r3[c] + r0[c]; // left
            p_[1][c] = r3[c] - r0[c]; // right
            p_[2][c] = r3[c] + r1[c]; // bottom
            p_[3][c] = r3[c] - r1[c]; // top
            p_[4][c] = r3[c] + r2[c]; // near
            p_[5][c] = r3[c] - r2[c]; // far
        }
    }

    // Conservative: false only when the box is certainly outside.
    bool intersectsBox(const Vec3& mn, const Vec3& mx) const
    {
        for (const auto& p : p_) {
            // The box corner furthest along the plane normal ("p-vertex").
            // If even that corner is behind the plane, the whole box is.
            float x = p[0] >= 0 ? mx.x : mn.x;
            float y = p[1] >= 0 ? mx.y : mn.y;
            float z = p[2] >= 0 ? mx.z : mn.z;
            if (p[0] * x + p[1] * y + p[2] * z + p[3] < 0.0f) return false;
        }
        return true;
    }

private:
    float p_[6][4];
};

} // namespace sea
