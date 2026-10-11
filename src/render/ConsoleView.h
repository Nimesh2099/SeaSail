// ConsoleView.h - draws the developer console: a panel that slides down
// from the top of the screen with the scroll-back log, the input line and
// a box of live suggestions under it.
#pragma once

namespace sea {

class Console;
class Hud;
class Shader;
class TextRenderer;

void drawConsole(Console& console, Hud& hud, TextRenderer& text, const Shader& hudShader, const Shader& textShader,
                 int screenW, int screenH);

} // namespace sea
