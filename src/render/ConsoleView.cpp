#include "ConsoleView.h"

#include "Console.h"
#include "Hud.h"
#include "TextRenderer.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace sea {

namespace {

Rgba colourFor(LineKind k)
{
    switch (k) {
    case LineKind::Echo:    return {1.00f, 0.80f, 0.42f, 1.0f};
    case LineKind::Error:   return {1.00f, 0.48f, 0.42f, 1.0f};
    case LineKind::Heading: return {0.52f, 0.86f, 1.00f, 1.0f};
    case LineKind::Info:    break;
    }
    return {0.86f, 0.89f, 0.91f, 1.0f};
}

struct Row {
    std::string text;
    LineKind kind;
};

// Wrap every log line to the panel width. Continuation rows are indented.
std::vector<Row> wrapLines(const Console& c, size_t cols)
{
    std::vector<Row> rows;
    cols = std::max<size_t>(cols, 8);
    for (const ConsoleLine& l : c.lines()) {
        if (l.text.size() <= cols) {
            rows.push_back({l.text, l.kind});
            continue;
        }
        size_t pos = 0;
        bool first = true;
        while (pos < l.text.size()) {
            size_t room = first ? cols : cols - 2;
            size_t n = std::min(room, l.text.size() - pos);
            // Prefer to break at a space.
            if (pos + n < l.text.size()) {
                size_t sp = l.text.rfind(' ', pos + n);
                if (sp != std::string::npos && sp > pos + room / 2) n = sp - pos + 1;
            }
            rows.push_back({(first ? "" : "  ") + l.text.substr(pos, n), l.kind});
            pos += n;
            first = false;
        }
    }
    return rows;
}

} // namespace

void drawConsole(Console& con, Hud& hud, TextRenderer& text, const Shader& hudShader, const Shader& textShader,
                 int w, int h)
{
    const float t = con.openAmount();
    if (t <= 0.0f) return;

    const int   ts   = std::max(2, h / 540);                 // text scale: 2 up to 1440p, 4 at 4K
    const float adv  = static_cast<float>(TextRenderer::kGlyph * ts);
    const float lineH = static_cast<float>(10 * ts);
    const float pad  = static_cast<float>(6 * ts);
    const float W = static_cast<float>(w), H = static_cast<float>(h);

    // Slide down from the top with an ease-out.
    const float ease = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
    const float panelH = std::floor(H * 0.45f);
    const float bottom = H - panelH * ease;

    const Rgba bg{0.03f, 0.07f, 0.10f, 0.90f}, bar{0.06f, 0.12f, 0.16f, 0.95f};
    const Rgba accent{1.0f, 0.75f, 0.35f, 0.95f}, faint{0.62f, 0.70f, 0.76f, 0.9f};

    hud.begin();
    text.begin();
    hud.rect(0.0f, bottom, W, panelH, bg);
    hud.rect(0.0f, bottom - static_cast<float>(ts), W, static_cast<float>(ts), accent);

    // ---- input line ----
    const float inputY = bottom + pad;
    hud.rect(0.0f, bottom, W, inputY + lineH + pad * 0.5f - bottom, bar);
    const std::string prompt = "> ";
    // Scroll the input sideways if it is wider than the screen.
    const size_t cols = static_cast<size_t>(std::max(8.0f, (W - 2.0f * pad) / adv));
    const std::string& in = con.input();
    size_t first = 0;
    if (con.cursor() + prompt.size() + 1 > cols) first = con.cursor() + prompt.size() + 1 - cols;
    text.text(pad, inputY, prompt, ts, accent);
    text.text(pad + adv * static_cast<float>(prompt.size()), inputY, in.substr(first, cols - prompt.size()), ts,
              Rgba{1, 1, 1, 1});
    if (con.isOpen() && con.caretVisible()) {
        float cx = pad + adv * static_cast<float>(prompt.size() + con.cursor() - first);
        hud.rect(std::floor(cx), inputY - static_cast<float>(ts), static_cast<float>(ts), static_cast<float>(10 * ts),
                 accent);
    }

    // ---- status strip: key help, or how far back we've scrolled ----
    const float stripY = inputY + lineH + pad * 0.5f;
    hud.rect(0.0f, stripY, W, static_cast<float>(std::max(1, ts / 2)), Rgba{1, 1, 1, 0.12f});

    // ---- log, newest at the bottom ----
    std::vector<Row> rows = wrapLines(con, cols);
    const float logBottom = stripY + pad * 0.75f;
    const int visible = std::max(1, static_cast<int>((H - pad * 0.5f - logBottom) / lineH));
    con.clampScroll(static_cast<int>(rows.size()) - visible);
    const int newest = static_cast<int>(rows.size()) - 1 - con.scroll();
    for (int i = 0; i < visible && newest - i >= 0; ++i) {
        const Row& r = rows[static_cast<size_t>(newest - i)];
        float y = logBottom + lineH * static_cast<float>(i);
        if (y + lineH > H + lineH) break;
        text.text(pad, y, r.text, ts, colourFor(r.kind));
    }

    std::string status = con.scroll() > 0
        ? "-- " + std::to_string(con.scroll()) + " lines back: PgDn / Ctrl+PgDn --"
        : "Tab complete   Up/Down history   PgUp/PgDn scroll   ` close";
    float sw = TextRenderer::width(status.size(), ts);
    // Key help sits in the empty input line; the scroll notice over the log.
    if (in.empty() && sw + 2.0f * pad < W - TextRenderer::width(prompt.size() + 1, ts))
        text.text(W - pad - sw, inputY, status, ts, Rgba{faint.r, faint.g, faint.b, 0.55f});
    else if (con.scroll() > 0)
        text.text(W - pad - sw, logBottom, status, ts, accent);

    // ---- suggestions under the panel ----
    if (con.isOpen() && t > 0.98f) {
        std::vector<std::string> hints = con.hints(8);
        if (!hints.empty()) {
            size_t longest = 0;
            for (const std::string& s : hints) longest = std::max(longest, s.size());
            longest = std::min(longest, cols - 2);
            float boxW = TextRenderer::width(longest, ts) + 2.0f * pad;
            float boxH = lineH * static_cast<float>(hints.size()) + pad;
            float top = bottom - static_cast<float>(ts);
            hud.rect(0.0f, top - boxH, boxW, boxH, Rgba{0.02f, 0.05f, 0.07f, 0.88f});
            for (size_t i = 0; i < hints.size(); ++i) {
                float y = top - pad * 0.5f - lineH * static_cast<float>(i + 1) + static_cast<float>(ts);
                text.text(pad, y, hints[i].substr(0, longest), ts, i == 0 ? Rgba{0.95f, 0.95f, 0.95f, 1.0f} : faint);
            }
        }
    }

    hud.flush(hudShader, w, h);
    text.flush(textShader, w, h);
}

} // namespace sea
