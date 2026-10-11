#include "TextRenderer.h"

#include "Font8x8.h"
#include "Shader.h"

#include <cmath>

namespace sea {

namespace {

// Atlas layout: 16 x 6 cells of 8x8, characters 32..127 in reading order.
constexpr int kCols = 16, kRows = 6;
constexpr int kAtlasW = kCols * TextRenderer::kGlyph, kAtlasH = kRows * TextRenderer::kGlyph;

} // namespace

TextRenderer::TextRenderer()
{
    // Unpack the font bits into one byte per pixel (0 or 255).
    std::vector<unsigned char> pixels(static_cast<size_t>(kAtlasW) * kAtlasH, 0);
    for (int c = kFontFirstChar; c <= kFontLastChar; ++c) {
        int cell = c - kFontFirstChar;
        int cx = (cell % kCols) * kGlyph, cy = (cell / kCols) * kGlyph;
        for (int row = 0; row < kGlyph; ++row) {
            unsigned char bits = kFont8x8[cell][row];
            for (int col = 0; col < kGlyph; ++col)
                if (bits & (1u << col)) pixels[static_cast<size_t>(cy + row) * kAtlasW + cx + col] = 255;
        }
    }

    glGenTextures(1, &tex_);
    glBindTexture(GL_TEXTURE_2D, tex_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, kAtlasW, kAtlasH, 0, GL_RED, GL_UNSIGNED_BYTE, pixels.data());
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    // Nearest filtering: crisp pixels at integer scales.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    const GLsizei stride = 8 * sizeof(float);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(2 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(4 * sizeof(float)));
    glBindVertexArray(0);
}

TextRenderer::~TextRenderer()
{
    glDeleteTextures(1, &tex_);
    glDeleteVertexArrays(1, &vao_);
    glDeleteBuffers(1, &vbo_);
}

void TextRenderer::glyph(float x, float y, unsigned char c, int scale, Rgba col)
{
    if (c == ' ') return;
    if (c < kFontFirstChar || c > kFontLastChar) c = '?';
    int cell = c - kFontFirstChar;
    // Texel rows run top-down in the atlas, so the quad's top edge uses the
    // cell's first row (v0) and its bottom edge the row below the cell (v1).
    float u0 = static_cast<float>((cell % kCols) * kGlyph) / kAtlasW;
    float u1 = u0 + static_cast<float>(kGlyph) / kAtlasW;
    float v0 = static_cast<float>((cell / kCols) * kGlyph) / kAtlasH;
    float v1 = v0 + static_cast<float>(kGlyph) / kAtlasH;
    float s = static_cast<float>(kGlyph * scale);
    const float quad[6][4] = {
        {x, y, u0, v1}, {x + s, y, u1, v1}, {x + s, y + s, u1, v0},
        {x, y, u0, v1}, {x + s, y + s, u1, v0}, {x, y + s, u0, v0},
    };
    for (const auto& q : quad) verts_.insert(verts_.end(), {q[0], q[1], q[2], q[3], col.r, col.g, col.b, col.a});
}

void TextRenderer::text(float x, float y, const std::string& s, int scale, Rgba color, bool shadow)
{
    // Snap to whole pixels so every font pixel covers exactly scale x scale.
    x = std::floor(x);
    y = std::floor(y);
    const float adv = static_cast<float>(kGlyph * scale);
    if (shadow) {
        const Rgba sh{0.0f, 0.0f, 0.0f, color.a * 0.6f};
        float off = static_cast<float>(scale > 1 ? scale / 2 : 1);
        for (size_t i = 0; i < s.size(); ++i)
            glyph(x + static_cast<float>(i) * adv + off, y - off, static_cast<unsigned char>(s[i]), scale, sh);
    }
    for (size_t i = 0; i < s.size(); ++i) glyph(x + static_cast<float>(i) * adv, y, static_cast<unsigned char>(s[i]), scale, color);
}

void TextRenderer::flush(const Shader& shader, int w, int h)
{
    if (verts_.empty()) return;
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(verts_.size() * sizeof(float)), verts_.data(), GL_STREAM_DRAW);
    shader.use();
    shader.setVec2("uScreen", static_cast<float>(w), static_cast<float>(h));
    shader.set("uFont", 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex_);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(verts_.size() / 8));
    glDisable(GL_BLEND);
    glBindVertexArray(0);
}

} // namespace sea
