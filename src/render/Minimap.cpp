#include "Minimap.h"

#include "IslandGenerator.h"
#include "Shader.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace sea {

namespace {

struct Rgb { float r, g, b; };

Rgb mixRgb(Rgb a, Rgb b, float t) { return {lerpf(a.r, b.r, t), lerpf(a.g, b.g, t), lerpf(a.b, b.b, t)}; }

// Chart colours. Water uses stepped depth bands like a nautical chart;
// land uses the same palette as the 3D terrain, a little brighter.
Rgb chartColour(float h, float moisture, float slope)
{
    if (h <= 0.0f) {
        const Rgb shallow{0.44f, 0.78f, 0.78f}, mid{0.22f, 0.55f, 0.66f}, deep{0.10f, 0.33f, 0.47f}, ocean{0.06f, 0.20f, 0.32f};
        if (h > -3.0f)  return mixRgb(shallow, mid, smoothstepf(-1.0f, -3.0f, h) * 0.3f);
        if (h > -10.0f) return mid;
        if (h > -25.0f) return deep;
        return ocean;
    }
    const Rgb sand{0.86f, 0.80f, 0.62f}, dry{0.58f, 0.56f, 0.33f}, lush{0.33f, 0.52f, 0.23f},
              forest{0.18f, 0.36f, 0.15f}, rock{0.50f, 0.48f, 0.45f};
    Rgb c = mixRgb(dry, lush, smoothstepf(0.3f, 0.6f, moisture));
    c = mixRgb(c, forest, smoothstepf(0.5f, 0.65f, moisture) * smoothstepf(4.0f, 9.0f, h));
    c = mixRgb(sand, c, smoothstepf(1.2f, 3.0f, h));
    c = mixRgb(c, rock, std::max(smoothstepf(0.35f, 0.55f, slope), smoothstepf(130.0f, 170.0f, h)));
    return c;
}

} // namespace

Minimap::Minimap()
{
    glGenTextures(1, &tex_);
    glBindTexture(GL_TEXTURE_2D, tex_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenVertexArrays(1, &vao_);
}

Minimap::~Minimap()
{
    if (job_.valid()) job_.wait();  // don't leave a thread writing into freed memory
    glDeleteTextures(1, &tex_);
    glDeleteVertexArrays(1, &vao_);
}

void Minimap::reset()
{
    if (job_.valid()) job_.wait();
    job_ = {};
    painting_ = false;
    hasTexture_ = false;
}

Minimap::Painting Minimap::paint(const IslandGenerator& world, double cx, double cz, float span)
{
    Painting p{cx, cz, span, {}};
    const int N = kSize;
    const float texel = span / N;

    // Heights first (one sample per texel), so shading and coastlines can
    // look at neighbours without sampling the world again.
    std::vector<float> h(static_cast<size_t>(N) * N), m(static_cast<size_t>(N) * N);
    for (int j = 0; j < N; ++j)
        for (int i = 0; i < N; ++i) {
            double x = cx - span * 0.5 + (i + 0.5) * texel;
            double z = cz - span * 0.5 + (j + 0.5) * texel;
            TerrainSample s = world.sample(x, z);
            h[static_cast<size_t>(j) * N + i] = s.height;
            m[static_cast<size_t>(j) * N + i] = s.moisture;
        }
    auto H = [&](int i, int j) {
        i = std::clamp(i, 0, N - 1);
        j = std::clamp(j, 0, N - 1);
        return h[static_cast<size_t>(j) * N + i];
    };

    p.rgb.resize(static_cast<size_t>(N) * N * 3);
    for (int j = 0; j < N; ++j)
        for (int i = 0; i < N; ++i) {
            float hc = H(i, j);
            float dhdx = (H(i + 1, j) - H(i - 1, j)) / (2.0f * texel);
            float dhdz = (H(i, j + 1) - H(i, j - 1)) / (2.0f * texel);
            float slope = 1.0f - 1.0f / std::sqrt(1.0f + dhdx * dhdx + dhdz * dhdz);
            Rgb c = chartColour(hc, m[static_cast<size_t>(j) * N + i], slope);

            if (hc > 0.0f) {
                // Hill shading, lit from the north-west as on paper maps.
                // A slope rising to the east or south faces north-west: brighter.
                float shade = clampf(1.0f + (dhdx + dhdz) * 0.8f, 0.6f, 1.3f);
                c = {c.r * shade, c.g * shade, c.b * shade};
            }
            // Coastline: land texel touching water gets a dark outline.
            bool land = hc > 0.0f;
            bool edge = land && (H(i + 1, j) <= 0.0f || H(i - 1, j) <= 0.0f || H(i, j + 1) <= 0.0f || H(i, j - 1) <= 0.0f);
            if (edge) c = {0.20f, 0.17f, 0.12f};
            // A faint ring where the water shelves to 10 m: the "safe water" line.
            bool shoal = !land && hc > -10.0f && (H(i + 1, j) <= -10.0f || H(i, j + 1) <= -10.0f);
            if (shoal) c = mixRgb(c, {0.9f, 0.95f, 0.95f}, 0.35f);

            unsigned char* px = &p.rgb[(static_cast<size_t>(j) * N + i) * 3];
            px[0] = static_cast<unsigned char>(clampf(c.r, 0.0f, 1.0f) * 255.0f);
            px[1] = static_cast<unsigned char>(clampf(c.g, 0.0f, 1.0f) * 255.0f);
            px[2] = static_cast<unsigned char>(clampf(c.b, 0.0f, 1.0f) * 255.0f);
        }
    return p;
}

void Minimap::update(const IslandGenerator& world, double boatX, double boatZ)
{
    // A finished painting? Upload it.
    if (painting_ && job_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        Painting p = job_.get();
        painting_ = false;
        glBindTexture(GL_TEXTURE_2D, tex_);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, kSize, kSize, 0, GL_RGB, GL_UNSIGNED_BYTE, p.rgb.data());
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        texCentreX_ = p.centreX;
        texCentreZ_ = p.centreZ;
        texSpan_ = p.span;
        hasTexture_ = true;
    }
    if (painting_) return;

    // Repaint when the visible circle is about to leave the painted square,
    // or when the zoom no longer matches.
    const float r = range();
    const float wantSpan = r * 2.6f;
    double dx = boatX - texCentreX_, dz = boatZ - texCentreZ_;
    bool offCentre = std::max(std::fabs(dx), std::fabs(dz)) > texSpan_ * 0.5 - r * 1.05;
    bool wrongZoom = std::fabs(texSpan_ - wantSpan) > 1.0f;
    if (!hasTexture_ || offCentre || wrongZoom) {
        painting_ = true;
        const IslandGenerator* w = &world;
        job_ = std::async(std::launch::async, [w, boatX, boatZ, wantSpan] { return paint(*w, boatX, boatZ, wantSpan); });
    }
}

void Minimap::draw(const Shader& shader, float cx, float cy, float radius, int screenW, int screenH,
                   double boatX, double boatZ, float night) const
{
    shader.use();
    shader.setVec2("uScreen", static_cast<float>(screenW), static_cast<float>(screenH));
    shader.setVec2("uCentre", cx, cy);
    shader.set("uRadius", radius);
    // Where the boat sits in the texture, and how much of it the circle shows.
    shader.setVec2("uOffset", static_cast<float>((boatX - texCentreX_) / texSpan_),
                   static_cast<float>((boatZ - texCentreZ_) / texSpan_));
    shader.set("uScale", range() / texSpan_);
    shader.set("uHasMap", hasTexture_ ? 1 : 0);
    shader.set("uNight", night);
    shader.set("uMap", 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex_);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisable(GL_BLEND);
}

} // namespace sea
