#include "Hud.h"

#include "Shader.h"

#include <cmath>

namespace sea {

Hud::Hud()
{
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<void*>(2 * sizeof(float)));
    glBindVertexArray(0);
}

Hud::~Hud()
{
    glDeleteVertexArrays(1, &vao_);
    glDeleteBuffers(1, &vbo_);
}

void Hud::triangle(float x0, float y0, float x1, float y1, float x2, float y2, Rgba c)
{
    verts_.insert(verts_.end(), {x0, y0, c.r, c.g, c.b, c.a, x1, y1, c.r, c.g, c.b, c.a, x2, y2, c.r, c.g, c.b, c.a});
}

void Hud::rect(float x, float y, float w, float h, Rgba c)
{
    triangle(x, y, x + w, y, x + w, y + h, c);
    triangle(x, y, x + w, y + h, x, y + h, c);
}

void Hud::line(float x0, float y0, float x1, float y1, float t, Rgba c)
{
    // A thin quad: core-profile OpenGL doesn't do wide lines.
    float dx = x1 - x0, dy = y1 - y0;
    float len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-4f) return;
    float nx = -dy / len * t * 0.5f, ny = dx / len * t * 0.5f;
    triangle(x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, c);
    triangle(x0 + nx, y0 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny, c);
}

void Hud::ring(float cx, float cy, float r, float t, Rgba c, int seg)
{
    for (int i = 0; i < seg; ++i) {
        float a0 = 2.0f * kPi * i / seg, a1 = 2.0f * kPi * (i + 1) / seg;
        float ri = r - t * 0.5f, ro = r + t * 0.5f;
        float c0 = std::cos(a0), s0 = std::sin(a0), c1 = std::cos(a1), s1 = std::sin(a1);
        triangle(cx + c0 * ri, cy + s0 * ri, cx + c0 * ro, cy + s0 * ro, cx + c1 * ro, cy + s1 * ro, c);
        triangle(cx + c0 * ri, cy + s0 * ri, cx + c1 * ro, cy + s1 * ro, cx + c1 * ri, cy + s1 * ri, c);
    }
}

void Hud::disc(float cx, float cy, float r, Rgba c, int seg)
{
    for (int i = 0; i < seg; ++i) {
        float a0 = 2.0f * kPi * i / seg, a1 = 2.0f * kPi * (i + 1) / seg;
        triangle(cx, cy, cx + std::cos(a0) * r, cy + std::sin(a0) * r, cx + std::cos(a1) * r, cy + std::sin(a1) * r, c);
    }
}

void Hud::flush(const Shader& shader, int w, int h)
{
    if (verts_.empty()) return;
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(verts_.size() * sizeof(float)), verts_.data(), GL_STREAM_DRAW);
    shader.use();
    shader.setVec2("uScreen", static_cast<float>(w), static_cast<float>(h));
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(verts_.size() / 6));
    glDisable(GL_BLEND);
    glBindVertexArray(0);
}

} // namespace sea
