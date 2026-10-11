// Mesh.h - simple coloured meshes built in code (no model files needed).
//
// Faces are flat-shaded: every triangle gets its own vertices and one
// normal, which gives a clean low-poly look.
#pragma once

#include "SeaMath.h"

#include <glad/gl.h>
#include <cstdint>
#include <vector>

namespace sea {

struct MeshVertex {
    float px, py, pz;
    float nx, ny, nz;
    float r, g, b;
    float emissive;  // 1 = glows (lamps, lit windows), ignores lighting
};

class MeshBuilder {
public:
    void triangle(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& color, float emissive = 0.0f);
    // Quad a-b-c-d, counter-clockwise seen from the front.
    void quad(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d, const Vec3& color, float emissive = 0.0f);
    // Axis-aligned box from min to max corner.
    void box(const Vec3& mn, const Vec3& mx, const Vec3& color, float emissive = 0.0f);
    // Box from a centre, half sizes, and a rotation.
    void orientedBox(const Vec3& centre, const Vec3& half, const Quat& rot, const Vec3& color, float emissive = 0.0f);
    // Capped cylinder along +Y from base, `sides` facets.
    void cylinder(const Vec3& base, float radius, float height, int sides, const Vec3& color, float emissive = 0.0f);
    // Tapered cylinder / cone along +Y (topRadius 0 = cone).
    void cone(const Vec3& base, float radius, float topRadius, float height, int sides, const Vec3& color, float emissive = 0.0f);
    // A tube through a list of points (e.g. a bent tree trunk), radius
    // interpolated from r0 at the first point to r1 at the last.
    void tube(const std::vector<Vec3>& path, float r0, float r1, int sides, const Vec3& color);
    // Low-poly sphere: an icosahedron, optionally subdivided once, with each
    // vertex pushed in/out by up to `lumpiness` (seeded) for rocks and foliage.
    void icoSphere(const Vec3& centre, const Vec3& radii, int subdivisions, float lumpiness, uint32_t seed, const Vec3& color);

    const std::vector<MeshVertex>& vertices() const { return verts_; }

private:
    std::vector<MeshVertex> verts_;
};

class Mesh {
public:
    Mesh() = default;
    ~Mesh();
    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;

    void upload(const MeshBuilder& b);
    void draw() const;

    GLuint vbo() const { return vbo_; }
    GLsizei vertexCount() const { return count_; }

private:
    GLuint vao_ = 0, vbo_ = 0;
    GLsizei count_ = 0;
};

} // namespace sea
