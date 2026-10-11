// TextRenderer.h - pixel-font text for the console and HUD labels.
//
// The 8x8 font is baked into a tiny texture atlas at start-up; each
// character is one textured quad, all batched into a single draw call.
// Text is drawn at whole-number scales so the pixels stay crisp.
#pragma once

#include "Hud.h"

#include <glad/gl.h>

#include <string>
#include <vector>

namespace sea {

class Shader;

class TextRenderer {
public:
    static constexpr int kGlyph = 8;  // glyph cell size in font pixels

    TextRenderer();
    ~TextRenderer();
    TextRenderer(const TextRenderer&) = delete;
    TextRenderer& operator=(const TextRenderer&) = delete;

    void begin() { verts_.clear(); }

    // Adds a string whose bottom-left corner is at (x, y) in pixels, with
    // (0, 0) at the bottom-left of the screen. Characters outside 32..126
    // are drawn as '?'. A soft drop shadow keeps it readable on any background.
    void text(float x, float y, const std::string& s, int scale, Rgba color, bool shadow = true);

    // Width in pixels of `chars` characters at `scale`.
    static float width(size_t chars, int scale) { return static_cast<float>(chars * kGlyph * scale); }

    void flush(const Shader& shader, int screenW, int screenH);

private:
    void glyph(float x, float y, unsigned char c, int scale, Rgba color);

    std::vector<float> verts_;  // x, y, u, v, r, g, b, a
    GLuint vao_ = 0, vbo_ = 0, tex_ = 0;
};

} // namespace sea
