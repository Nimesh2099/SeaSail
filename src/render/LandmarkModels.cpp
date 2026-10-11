#include "LandmarkModels.h"

#include <cmath>
#include <vector>

namespace sea {

void buildLighthouse(MeshBuilder& m)
{
    const Vec3 white{0.88f, 0.87f, 0.83f}, red{0.62f, 0.10f, 0.08f}, dark{0.12f, 0.13f, 0.14f},
               stone{0.42f, 0.40f, 0.37f}, lamp{1.0f, 0.86f, 0.55f};
    // Stone plinth, sunk into the ground so slopes don't show a gap.
    m.cone({0, -2.0f, 0}, 3.2f, 3.0f, 3.0f, 10, stone);
    // Tapered tower in red and white bands.
    const float base = 1.0f, top = 15.0f;
    const int bands = 5;
    for (int b = 0; b < bands; ++b) {
        float y0 = base + (top - base) * b / bands, y1 = base + (top - base) * (b + 1) / bands;
        float r0 = lerpf(2.2f, 1.4f, (y0 - base) / (top - base)), r1 = lerpf(2.2f, 1.4f, (y1 - base) / (top - base));
        m.cone({0, y0, 0}, r0, r1, y1 - y0, 12, (b % 2) ? red : white);
    }
    // Door and windows.
    m.box({2.05f, base, -0.45f}, {2.25f, base + 2.0f, 0.45f}, dark);
    m.box({1.75f, 8.0f, -0.25f}, {1.95f, 9.0f, 0.25f}, dark);
    m.box({-1.95f, 11.0f, -0.25f}, {-1.75f, 12.0f, 0.25f}, dark);
    // Gallery (balcony), lantern room, roof.
    m.cylinder({0, top, 0}, 2.0f, 0.25f, 12, dark);
    for (int i = 0; i < 12; ++i) {  // railing posts
        float a = 2.0f * kPi * i / 12;
        m.box({std::cos(a) * 1.9f - 0.04f, top + 0.25f, std::sin(a) * 1.9f - 0.04f},
              {std::cos(a) * 1.9f + 0.04f, top + 1.1f, std::sin(a) * 1.9f + 0.04f}, dark);
    }
    m.cylinder({0, top + 0.25f, 0}, 1.15f, 2.6f, 10, lamp, 1.0f);       // the glowing lamp room
    m.cone({0, top + 2.85f, 0}, 1.45f, 0.0f, 1.6f, 10, red);           // roof
    m.cylinder({0, top + 4.4f, 0}, 0.06f, 0.8f, 4, dark);              // finial
}

void buildJetty(MeshBuilder& m)
{
    const Vec3 plank{0.45f, 0.34f, 0.22f}, plankDark{0.36f, 0.27f, 0.17f}, post{0.25f, 0.19f, 0.13f}, rope{0.70f, 0.62f, 0.45f};
    const float w = 1.3f;  // half width
    // Planks across the deck, alternating shades so it reads as boards.
    for (int i = 0; i < 40; ++i) {
        float x0 = -4.0f + i * 0.85f, x1 = x0 + 0.78f;
        if (x0 > kJettyLength) break;
        m.box({x0, -0.12f, -w}, {x1, 0.0f, w}, (i % 3 == 0) ? plankDark : plank);
    }
    // Stringers and posts down into the water and sea bed.
    m.box({-4.0f, -0.4f, -w + 0.1f}, {kJettyLength, -0.12f, -w + 0.3f}, post);
    m.box({-4.0f, -0.4f, w - 0.3f}, {kJettyLength, -0.12f, w - 0.1f}, post);
    for (float x = 0.0f; x <= kJettyLength + 0.1f; x += 5.0f)
        for (float z : {-w, w}) {
            m.cylinder({x, -8.0f, z}, 0.16f, 9.0f, 6, post);
            m.box({x - 0.05f, 0.95f, z - 0.05f}, {x + 0.05f, 1.0f, z + 0.05f}, rope);
        }
    // Rope rail along both sides.
    m.box({0.0f, 0.9f, -w - 0.03f}, {kJettyLength, 0.94f, -w + 0.03f}, rope);
    m.box({0.0f, 0.9f, w - 0.03f}, {kJettyLength, 0.94f, w + 0.03f}, rope);
    // A couple of crates and a lantern post at the end.
    m.box({kJettyLength - 2.6f, 0.0f, 0.3f}, {kJettyLength - 1.8f, 0.7f, 1.1f}, {0.50f, 0.36f, 0.19f});
    m.cylinder({kJettyLength - 0.5f, 0.0f, -w + 0.2f}, 0.07f, 2.2f, 5, post);
    m.box({kJettyLength - 0.65f, 2.2f, -w + 0.05f}, {kJettyLength - 0.35f, 2.55f, -w + 0.35f}, {1.0f, 0.75f, 0.4f}, 1.0f);
}

void buildHut(MeshBuilder& m)
{
    const Vec3 wall{0.50f, 0.40f, 0.30f}, wallDark{0.40f, 0.31f, 0.23f}, roof{0.30f, 0.22f, 0.20f},
               stilt{0.25f, 0.19f, 0.13f}, door{0.20f, 0.15f, 0.10f};
    // On short stilts so it sits level on a sloping beach.
    for (float x : {-1.6f, 1.6f})
        for (float z : {-1.6f, 1.6f}) m.cylinder({x, -1.5f, z}, 0.12f, 2.0f, 5, stilt);
    m.box({-1.8f, 0.4f, -1.8f}, {1.8f, 0.55f, 1.8f}, wallDark);   // floor
    m.box({-1.7f, 0.55f, -1.7f}, {1.7f, 2.6f, 1.7f}, wall);        // walls
    m.box({1.70f, 0.55f, -0.45f}, {1.75f, 2.1f, 0.45f}, door);
    m.box({-0.5f, 1.4f, 1.70f}, {0.5f, 2.0f, 1.75f}, {0.08f, 0.10f, 0.12f});   // window
    // Pitched roof: two sloping slabs.
    Quat tilt = Quat::fromAxisAngle({1, 0, 0}, 0.5f);
    m.orientedBox({0, 3.05f, 0.95f}, {2.1f, 0.08f, 1.15f}, tilt, roof);
    m.orientedBox({0, 3.05f, -0.95f}, {2.1f, 0.08f, 1.15f}, tilt.conjugate(), roof);
    m.quad({-1.7f, 2.6f, -1.7f}, {-1.7f, 2.6f, 1.7f}, {-1.7f, 3.55f, 0.0f}, {-1.7f, 3.55f, 0.0f}, wall); // gable
    m.quad({1.7f, 2.6f, 1.7f}, {1.7f, 2.6f, -1.7f}, {1.7f, 3.55f, 0.0f}, {1.7f, 3.55f, 0.0f}, wall);
    // Lantern by the door, nets drying on a frame.
    m.box({1.75f, kHutLanternY - 0.15f, 0.65f}, {1.95f, kHutLanternY + 0.15f, 0.85f}, {1.0f, 0.75f, 0.4f}, 1.0f);
    m.box({2.6f, 0.0f, -1.2f}, {2.7f, 1.6f, -1.1f}, stilt);
    m.box({2.6f, 0.0f, 1.1f}, {2.7f, 1.6f, 1.2f}, stilt);
    m.box({2.62f, 0.6f, -1.1f}, {2.66f, 1.55f, 1.1f}, {0.55f, 0.52f, 0.42f});
}

// ------------------------------------------------------------------- beam ---

BeamMesh::BeamMesh()
{
    // Vertex: position (x along the beam, y, z) + (t along 0..1, s across -1..1).
    std::vector<float> v;
    const float L = 260.0f;
    const int segs = 12;
    auto push = [&](float x, float y, float z, float t, float s) { v.insert(v.end(), {x, y, z, t, s}); };
    for (int plane = 0; plane < 2; ++plane)
        for (int i = 0; i < segs; ++i) {
            float t0 = static_cast<float>(i) / segs, t1 = static_cast<float>(i + 1) / segs;
            float w0 = 0.6f + 16.0f * t0, w1 = 0.6f + 16.0f * t1;
            auto P = [&](float t, float w, float s) {
                return plane == 0 ? Vec3{t * L, 0.0f, w * s} : Vec3{t * L, w * s * 0.6f, 0.0f};
            };
            Vec3 a = P(t0, w0, -1), b = P(t1, w1, -1), c = P(t1, w1, 1), d = P(t0, w0, 1);
            push(a.x, a.y, a.z, t0, -1); push(b.x, b.y, b.z, t1, -1); push(c.x, c.y, c.z, t1, 1);
            push(a.x, a.y, a.z, t0, -1); push(c.x, c.y, c.z, t1, 1); push(d.x, d.y, d.z, t0, 1);
        }
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(v.size() * sizeof(float)), v.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));
    glBindVertexArray(0);
    count_ = static_cast<GLsizei>(v.size() / 5);
}

BeamMesh::~BeamMesh()
{
    glDeleteVertexArrays(1, &vao_);
    glDeleteBuffers(1, &vbo_);
}

void BeamMesh::draw() const
{
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, count_);
}

} // namespace sea
