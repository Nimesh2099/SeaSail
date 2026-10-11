// Hud.h - minimal 2D overlay drawn in pixel coordinates (compass, gauges).
// Shapes are batched into one vertex buffer and drawn in a single call.
#pragma once

#include "SeaMath.h"

#include <glad/gl.h>
#include <vector>

namespace sea {

class Shader;

struct Rgba { float r, g, b, a; };

class Hud {
public:
    Hud();
    ~Hud();
    Hud(const Hud&) = delete;
    Hud& operator=(const Hud&) = delete;

    void begin() { verts_.clear(); }
    void triangle(float x0, float y0, float x1, float y1, float x2, float y2, Rgba c);
    void rect(float x, float y, float w, float h, Rgba c);
    void line(float x0, float y0, float x1, float y1, float thickness, Rgba c);
    void ring(float cx, float cy, float radius, float thickness, Rgba c, int segments = 48);
    void disc(float cx, float cy, float radius, Rgba c, int segments = 48);
    // Draws everything added since begin(). (0, 0) is the bottom-left pixel.
    void flush(const Shader& shader, int screenW, int screenH);

private:
    std::vector<float> verts_; // x, y, r, g, b, a
    GLuint vao_ = 0, vbo_ = 0;
};

} // namespace sea
