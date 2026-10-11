// Minimap.h - a round sea chart in the corner of the screen.
//
// The chart is a small texture painted on the CPU from the island generator
// (terrain colours, hill shading, depth bands, dark coastlines). Painting it
// takes ~0.1 s, so it happens on a background thread: the old chart keeps
// being drawn (correctly offset) until the new one is ready, then they swap.
// It's repainted when the boat nears the edge of the painted area or the
// zoom changes.
#pragma once

#include "SeaMath.h"

#include <glad/gl.h>

#include <future>
#include <vector>

namespace sea {

class IslandGenerator;
class Shader;

class Minimap {
public:
    Minimap();
    ~Minimap();
    Minimap(const Minimap&) = delete;
    Minimap& operator=(const Minimap&) = delete;

    // How far (metres) from the boat to the edge of the circle, per zoom level.
    static constexpr float kRanges[3] = {400.0f, 1000.0f, 2500.0f};

    void cycleZoom() { zoom_ = (zoom_ + 1) % 3; }
    float range() const { return kRanges[zoom_]; }

    // Forget the chart (e.g. after generating a new world).
    void reset();

    // Start a repaint if needed; upload a finished one. Call once per frame.
    void update(const IslandGenerator& world, double boatX, double boatZ);

    // Draw the chart into a circle of `radius` pixels centred at (cx, cy).
    void draw(const Shader& shader, float cx, float cy, float radius, int screenW, int screenH,
              double boatX, double boatZ, float night) const;

private:
    struct Painting {
        double centreX, centreZ;
        float  span;                    // metres covered by the texture edge to edge
        std::vector<unsigned char> rgb; // kSize * kSize * 3
    };
    static constexpr int kSize = 192;   // texels per side
    static Painting paint(const IslandGenerator& world, double cx, double cz, float span);

    GLuint tex_ = 0, vao_ = 0;
    bool   hasTexture_ = false;
    double texCentreX_ = 0, texCentreZ_ = 0;
    float  texSpan_ = 1.0f;
    int    zoom_ = 1;
    bool   painting_ = false;
    std::future<Painting> job_;
};

} // namespace sea
