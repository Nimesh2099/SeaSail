#include "PropRenderer.h"

#include "Shader.h"

#include <cmath>
#include <cstddef>

namespace sea {

// ------------------------------------------------------------------ models ---
// All in metres, base at the origin, +Y up. Instances scale and rotate them.

void buildPalm(MeshBuilder& m)
{
    const Vec3 bark{0.40f, 0.33f, 0.24f}, frond{0.17f, 0.36f, 0.09f}, frondDark{0.12f, 0.28f, 0.07f}, nut{0.30f, 0.20f, 0.10f};
    // A trunk that leans and curves (palms rarely grow straight up).
    std::vector<Vec3> trunk;
    const float H = 7.5f;
    for (int i = 0; i <= 7; ++i) {
        float t = i / 7.0f;
        trunk.push_back({1.6f * t * t, H * t, 0.0f});
    }
    m.tube(trunk, 0.24f, 0.13f, 6, bark);
    Vec3 top = trunk.back();

    // Fronds: long leaves that arch out and droop, each folded slightly
    // along its spine so it catches light on two faces.
    const int fronds = 9;
    for (int f = 0; f < fronds; ++f) {
        float a = 2.0f * kPi * f / fronds + (f % 2) * 0.2f;
        Vec3 dir{std::cos(a), 0.0f, std::sin(a)};
        Vec3 side = normalize(cross(dir, Vec3{0, 1, 0}));
        float lift = (f % 3) * 0.25f;
        Vec3 prev = top;
        float prevW = 0.05f;
        for (int s = 1; s <= 5; ++s) {
            float t = s / 5.0f;
            Vec3 p = top + dir * (3.4f * t) + Vec3{0.0f, (0.9f + lift) * t - 2.4f * t * t, 0.0f};
            float w = 0.55f * std::sin(kPi * std::min(t * 1.15f, 1.0f)) + 0.04f;
            Vec3 fold{0.0f, 0.12f * w, 0.0f};
            Vec3 c = (f % 2) ? frond : frondDark;
            m.quad(prev, prev + side * prevW - fold, p + side * w - fold, p, c);
            m.quad(prev, p, p - side * w - fold, prev - side * prevW - fold, c);
            prev = p;
            prevW = w;
        }
    }
    for (int i = 0; i < 3; ++i) {
        float a = i * 2.1f;
        m.icoSphere(top + Vec3{std::cos(a) * 0.25f, -0.3f, std::sin(a) * 0.25f}, {0.16f, 0.18f, 0.16f}, 0, 0.0f, 1, nut);
    }
}

void buildBroadleaf(MeshBuilder& m)
{
    const Vec3 bark{0.30f, 0.23f, 0.16f}, leaf{0.13f, 0.30f, 0.08f}, leafLight{0.18f, 0.36f, 0.10f};
    m.cone({0, -0.5f, 0}, 0.32f, 0.16f, 4.4f, 6, bark);
    m.tube({{0.0f, 2.6f, 0.0f}, {0.9f, 3.6f, 0.3f}, {1.4f, 4.3f, 0.5f}}, 0.12f, 0.07f, 5, bark);  // a branch
    m.icoSphere({0.0f, 5.2f, 0.0f}, {2.4f, 1.9f, 2.4f}, 1, 0.22f, 11, leaf);
    m.icoSphere({1.3f, 4.6f, 0.6f}, {1.6f, 1.3f, 1.6f}, 1, 0.25f, 12, leafLight);
    m.icoSphere({-1.1f, 4.4f, -0.8f}, {1.5f, 1.2f, 1.5f}, 1, 0.25f, 13, leaf);
    m.icoSphere({0.2f, 6.4f, -0.3f}, {1.4f, 1.1f, 1.4f}, 1, 0.25f, 14, leafLight);
}

void buildPine(MeshBuilder& m)
{
    const Vec3 bark{0.28f, 0.20f, 0.14f}, needles{0.06f, 0.19f, 0.09f}, needlesLight{0.08f, 0.23f, 0.11f};
    m.cylinder({0, -0.5f, 0}, 0.2f, 3.0f, 6, bark);
    m.cone({0, 1.8f, 0}, 2.3f, 0.0f, 4.2f, 8, needles);
    m.cone({0, 3.8f, 0}, 1.8f, 0.0f, 3.8f, 8, needlesLight);
    m.cone({0, 5.8f, 0}, 1.2f, 0.0f, 3.4f, 7, needles);
}

void buildBush(MeshBuilder& m)
{
    const Vec3 leaf{0.20f, 0.30f, 0.10f}, leafDark{0.14f, 0.24f, 0.08f};
    m.icoSphere({0.0f, 0.5f, 0.0f}, {1.1f, 0.8f, 1.0f}, 1, 0.25f, 21, leaf);
    m.icoSphere({0.7f, 0.35f, 0.4f}, {0.7f, 0.55f, 0.7f}, 0, 0.2f, 22, leafDark);
}

void buildRock(MeshBuilder& m)
{
    m.icoSphere({0.0f, 0.35f, 0.0f}, {1.2f, 0.8f, 1.0f}, 1, 0.3f, 31, {0.34f, 0.32f, 0.30f});
}

void buildGull(MeshBuilder& m)
{
    // Faces +X; wings span +-Z. The vertex shader flaps anything away from
    // the body (|z| > 0) up and down.
    const Vec3 white{0.92f, 0.92f, 0.90f}, grey{0.55f, 0.57f, 0.60f}, beak{0.85f, 0.65f, 0.15f};
    m.orientedBox({0, 0, 0}, {0.22f, 0.07f, 0.06f}, Quat::identity(), white);
    m.box({0.22f, -0.02f, -0.02f}, {0.30f, 0.02f, 0.02f}, beak);
    for (float side : {1.0f, -1.0f}) {
        Vec3 root0{0.08f, 0.02f, 0.05f * side}, root1{-0.10f, 0.02f, 0.05f * side};
        Vec3 mid0{0.02f, 0.05f, 0.40f * side}, mid1{-0.14f, 0.05f, 0.40f * side};
        Vec3 tip{-0.20f, 0.0f, 0.72f * side};
        m.quad(root0, mid0, mid1, root1, white);
        m.triangle(mid0, tip, mid1, grey);
    }
}

// ---------------------------------------------------------------- renderer ---

PropRenderer::PropRenderer()
{
    void (*builders[kPropTypes])(MeshBuilder&) = {buildPalm, buildBroadleaf, buildPine, buildBush, buildRock, buildGull};
    const float sway[kPropTypes] = {1.0f, 0.45f, 0.3f, 0.35f, 0.0f, 0.0f};

    for (int t = 0; t < kPropTypes; ++t) {
        Kind& k = kinds_[t];
        MeshBuilder mb;
        builders[t](mb);
        k.mesh.upload(mb);
        k.sway = sway[t];
        k.flap = t == static_cast<int>(PropType::Gull) ? 1.0f : 0.0f;

        // A VAO that reads the model from the mesh's buffer (one vertex per
        // vertex) and the instance data from its own buffer (one entry per
        // instance, advanced by glVertexAttribDivisor).
        glGenVertexArrays(1, &k.vao);
        glGenBuffers(1, &k.instanceVbo);
        glBindVertexArray(k.vao);

        glBindBuffer(GL_ARRAY_BUFFER, k.mesh.vbo());
        const GLsizei vs = sizeof(MeshVertex);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, vs, reinterpret_cast<void*>(offsetof(MeshVertex, px)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, vs, reinterpret_cast<void*>(offsetof(MeshVertex, nx)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, vs, reinterpret_cast<void*>(offsetof(MeshVertex, r)));

        glBindBuffer(GL_ARRAY_BUFFER, k.instanceVbo);
        const GLsizei is = sizeof(PropInstance);
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, is, reinterpret_cast<void*>(offsetof(PropInstance, x)));
        glVertexAttribDivisor(3, 1);
        glEnableVertexAttribArray(4);
        glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, is, reinterpret_cast<void*>(offsetof(PropInstance, yaw)));
        glVertexAttribDivisor(4, 1);
        glBindVertexArray(0);
    }
}

PropRenderer::~PropRenderer()
{
    for (Kind& k : kinds_) {
        glDeleteVertexArrays(1, &k.vao);
        glDeleteBuffers(1, &k.instanceVbo);
    }
}

void PropRenderer::draw(const Shader& shader, const PropLists& lists)
{
    for (int t = 0; t < kPropTypes; ++t) {
        const auto& list = lists[t];
        if (list.empty()) continue;
        Kind& k = kinds_[t];
        glBindBuffer(GL_ARRAY_BUFFER, k.instanceVbo);
        // Re-specify the whole buffer each frame ("orphaning"), so the driver
        // never has to wait for last frame's draw to finish reading it.
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(list.size() * sizeof(PropInstance)), list.data(), GL_STREAM_DRAW);
        shader.set("uSway", k.sway);
        shader.set("uFlap", k.flap);
        glBindVertexArray(k.vao);
        glDrawArraysInstanced(GL_TRIANGLES, 0, k.mesh.vertexCount(), static_cast<GLsizei>(list.size()));
    }
    glBindVertexArray(0);
}

} // namespace sea
