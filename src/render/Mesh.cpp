#include "Mesh.h"

#include <cmath>
#include <cstddef>

namespace sea {

void MeshBuilder::triangle(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& color, float emissive)
{
    Vec3 n = normalize(cross(b - a, c - a));
    for (const Vec3* p : {&a, &b, &c})
        verts_.push_back({p->x, p->y, p->z, n.x, n.y, n.z, color.x, color.y, color.z, emissive});
}

void MeshBuilder::quad(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d, const Vec3& color, float emissive)
{
    triangle(a, b, c, color, emissive);
    triangle(a, c, d, color, emissive);
}

void MeshBuilder::box(const Vec3& mn, const Vec3& mx, const Vec3& color, float emissive)
{
    Vec3 c = (mn + mx) * 0.5f, h = (mx - mn) * 0.5f;
    orientedBox(c, h, Quat::identity(), color, emissive);
}

void MeshBuilder::orientedBox(const Vec3& centre, const Vec3& half, const Quat& rot, const Vec3& color, float emissive)
{
    auto P = [&](float sx, float sy, float sz) {
        return centre + rot.rotate({sx * half.x, sy * half.y, sz * half.z});
    };
    // Each face listed counter-clockwise as seen from outside.
    quad(P(1, -1, -1), P(1, 1, -1), P(1, 1, 1), P(1, -1, 1), color, emissive);       // +X
    quad(P(-1, -1, 1), P(-1, 1, 1), P(-1, 1, -1), P(-1, -1, -1), color, emissive);   // -X
    quad(P(-1, 1, -1), P(-1, 1, 1), P(1, 1, 1), P(1, 1, -1), color, emissive);       // +Y
    quad(P(-1, -1, 1), P(-1, -1, -1), P(1, -1, -1), P(1, -1, 1), color, emissive);   // -Y
    quad(P(-1, -1, 1), P(1, -1, 1), P(1, 1, 1), P(-1, 1, 1), color, emissive);       // +Z
    quad(P(1, -1, -1), P(-1, -1, -1), P(-1, 1, -1), P(1, 1, -1), color, emissive);   // -Z
}

void MeshBuilder::cylinder(const Vec3& base, float radius, float height, int sides, const Vec3& color, float emissive)
{
    Vec3 top = base + Vec3{0, height, 0};
    for (int i = 0; i < sides; ++i) {
        float a0 = 2.0f * kPi * i / sides, a1 = 2.0f * kPi * (i + 1) / sides;
        Vec3 o0{std::cos(a0) * radius, 0, std::sin(a0) * radius};
        Vec3 o1{std::cos(a1) * radius, 0, std::sin(a1) * radius};
        quad(base + o1, base + o0, top + o0, top + o1, color, emissive);
        triangle(top, top + o1, top + o0, color, emissive);
        triangle(base, base + o0, base + o1, color, emissive);
    }
}

void MeshBuilder::cone(const Vec3& base, float radius, float topRadius, float height, int sides, const Vec3& color, float emissive)
{
    Vec3 top = base + Vec3{0, height, 0};
    for (int i = 0; i < sides; ++i) {
        float a0 = 2.0f * kPi * i / sides, a1 = 2.0f * kPi * (i + 1) / sides;
        Vec3 d0{std::cos(a0), 0, std::sin(a0)}, d1{std::cos(a1), 0, std::sin(a1)};
        if (topRadius > 0.0f) {
            quad(base + d1 * radius, base + d0 * radius, top + d0 * topRadius, top + d1 * topRadius, color, emissive);
            triangle(top, top + d1 * topRadius, top + d0 * topRadius, color, emissive);
        } else {
            triangle(base + d1 * radius, base + d0 * radius, top, color, emissive);
        }
        triangle(base, base + d0 * radius, base + d1 * radius, color, emissive);
    }
}

void MeshBuilder::tube(const std::vector<Vec3>& path, float r0, float r1, int sides, const Vec3& color)
{
    if (path.size() < 2) return;
    // A ring of points around each path point, perpendicular to the path.
    std::vector<std::vector<Vec3>> rings;
    for (size_t i = 0; i < path.size(); ++i) {
        Vec3 dir = i + 1 < path.size() ? path[i + 1] - path[i] : path[i] - path[i - 1];
        dir = normalize(dir);
        Vec3 u = anyPerpendicular(dir), v = cross(dir, u);
        float r = lerpf(r0, r1, static_cast<float>(i) / static_cast<float>(path.size() - 1));
        std::vector<Vec3> ring;
        for (int k = 0; k < sides; ++k) {
            float a = 2.0f * kPi * k / sides;
            ring.push_back(path[i] + (u * std::cos(a) + v * std::sin(a)) * r);
        }
        rings.push_back(ring);
    }
    for (size_t i = 0; i + 1 < rings.size(); ++i)
        for (int k = 0; k < sides; ++k) {
            int k1 = (k + 1) % sides;
            quad(rings[i][k], rings[i][k1], rings[i + 1][k1], rings[i + 1][k], color);
        }
}

void MeshBuilder::icoSphere(const Vec3& c, const Vec3& radii, int subdivisions, float lumpiness, uint32_t seed, const Vec3& color)
{
    // The 12 vertices of an icosahedron and its 20 faces.
    const float t = (1.0f + std::sqrt(5.0f)) * 0.5f;
    std::vector<Vec3> v = {
        {-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0},
        {0, -1, t}, {0, 1, t}, {0, -1, -t}, {0, 1, -t},
        {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1}};
    for (Vec3& p : v) p = normalize(p);
    std::vector<int> f = {0, 11, 5, 0, 5, 1, 0, 1, 7, 0, 7, 10, 0, 10, 11, 1, 5, 9, 5, 11, 4, 11, 10, 2, 10, 7, 6, 7, 1, 8,
                          3, 9, 4, 3, 4, 2, 3, 2, 6, 3, 6, 8, 3, 8, 9, 4, 9, 5, 2, 4, 11, 6, 2, 10, 8, 6, 7, 9, 8, 1};
    for (int s = 0; s < subdivisions; ++s) {
        std::vector<int> nf;
        auto mid = [&](int a, int b) {
            v.push_back(normalize((v[a] + v[b]) * 0.5f));  // duplicates are fine: we flat-shade anyway
            return static_cast<int>(v.size()) - 1;
        };
        for (size_t i = 0; i < f.size(); i += 3) {
            int a = f[i], b = f[i + 1], cc = f[i + 2];
            int ab = mid(a, b), bc = mid(b, cc), ca = mid(cc, a);
            nf.insert(nf.end(), {a, ab, ca, b, bc, ab, cc, ca, bc, ab, bc, ca});
        }
        f.swap(nf);
    }
    // Lumps: scale each vertex by a hash of its (rounded) direction, so
    // duplicated midpoints at the same spot move together and no cracks open.
    auto lump = [&](const Vec3& p) {
        int32_t qx = static_cast<int32_t>(std::lround(p.x * 1000)), qy = static_cast<int32_t>(std::lround(p.y * 1000)),
                qz = static_cast<int32_t>(std::lround(p.z * 1000));
        uint32_t h = static_cast<uint32_t>(qx) * 73856093u ^ static_cast<uint32_t>(qy) * 19349663u ^ static_cast<uint32_t>(qz) * 83492791u ^ seed * 2654435761u;
        h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
        return 1.0f + lumpiness * ((h & 0xffff) / 65535.0f * 2.0f - 1.0f);
    };
    for (size_t i = 0; i < f.size(); i += 3) {
        Vec3 p[3];
        for (int k = 0; k < 3; ++k) {
            Vec3 d = v[f[i + k]];
            float l = lump(d);
            p[k] = c + Vec3{d.x * radii.x, d.y * radii.y, d.z * radii.z} * l;
        }
        triangle(p[0], p[1], p[2], color);
    }
}

Mesh::~Mesh()
{
    if (vao_) {
        glDeleteVertexArrays(1, &vao_);
        glDeleteBuffers(1, &vbo_);
    }
}

void Mesh::upload(const MeshBuilder& b)
{
    if (!vao_) {
        glGenVertexArrays(1, &vao_);
        glGenBuffers(1, &vbo_);
    }
    const auto& v = b.vertices();
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(v.size() * sizeof(MeshVertex)), v.data(), GL_STATIC_DRAW);
    const GLsizei stride = sizeof(MeshVertex);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(MeshVertex, px)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(MeshVertex, nx)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(MeshVertex, r)));
    glBindVertexArray(0);
    count_ = static_cast<GLsizei>(v.size());
}

void Mesh::draw() const
{
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, count_);
}

} // namespace sea
