// Console.h - an in-game developer console (open it with the ` key).
//
// Commands are registered with a name, a usage string, a one-line help and
// a function that receives the arguments. A line can hold several commands
// separated by ';', and arguments with spaces can be "quoted". The console
// keeps a scroll-back log, command history (Up / Down), Tab completion of
// command names and of each command's known options, and can run a file of
// commands with `exec`.
//
// This class only knows about text. Drawing it is ConsoleView's job and
// feeding it keys is main.cpp's, so it has no OpenGL or GLFW dependency.
#pragma once

#include <cstdarg>
#include <deque>
#include <functional>
#include <string>
#include <vector>

namespace sea {

using ConsoleArgs = std::vector<std::string>;

struct ConsoleCommand {
    std::string name;
    std::string category;              // groups commands in `help`
    std::string usage;                 // e.g. "<x> <z>" or "[on|off]"
    std::string help;                  // one line
    std::function<void(const ConsoleArgs&)> run;
    std::vector<std::string> options;  // Tab-completion words for the first argument
};

enum class LineKind { Info, Echo, Error, Heading };

struct ConsoleLine {
    std::string text;
    LineKind kind;
};

// Editing keys the console understands (main.cpp maps GLFW keys to these).
enum class ConsoleKey { Enter, Backspace, Delete, Left, Right, Home, End, Up, Down, Tab, PageUp, PageDown };

class Console {
public:
    Console();

    void add(ConsoleCommand cmd);
    const ConsoleCommand* find(const std::string& name) const;
    const std::vector<ConsoleCommand>& commands() const { return commands_; }

    // Run a line of one or more commands. `echo` prints "> line" first.
    void execute(const std::string& line, bool echo = true);
    // Run every line of a text file ('#' and '//' start comments).
    bool execFile(const std::string& path);

    // Output. Everything printed also goes to the terminal.
    void print(const char* fmt, ...);
    void error(const char* fmt, ...);
    void heading(const char* fmt, ...);
    void clear() { lines_.clear(); scroll_ = 0; }

    // ---- input ----
    bool isOpen() const { return open_; }
    void setOpen(bool open);
    void toggle() { setOpen(!open_); }
    void onChar(unsigned int codepoint);
    void onKey(ConsoleKey key, bool ctrl);
    void insertText(const std::string& s);  // paste
    void scrollBy(int rows);

    // ---- for drawing ----
    void  update(float dt);                  // slide animation, caret blink
    float openAmount() const { return slide_; }
    bool  caretVisible() const { return blink_ < 0.5f; }
    const std::deque<ConsoleLine>& lines() const { return lines_; }
    const std::string& input() const { return input_; }
    size_t cursor() const { return cursor_; }
    int    scroll() const { return scroll_; }
    void   clampScroll(int maxRows);
    // Live hints for what's being typed: matching commands with their
    // usage, or the usage and options of the command being typed.
    std::vector<std::string> hints(size_t max) const;

    // ---- argument helpers for command functions ----
    // Parse a number; prints an error naming `what` and returns false if invalid.
    bool number(const std::string& s, double& out, const char* what);
    bool number(const std::string& s, float& out, const char* what);
    // "on/off/1/0/true/false/yes/no" -> value; no argument -> toggle `current`.
    bool onOff(const ConsoleArgs& args, bool current, bool& out);
    static std::string lower(std::string s);

private:
    void log(LineKind kind, const char* fmt, va_list args);
    void runOne(const std::vector<std::string>& tokens);
    void complete();

    std::vector<ConsoleCommand> commands_;
    std::deque<ConsoleLine> lines_;
    std::vector<std::string> history_;
    int historyPos_ = -1;   // -1 = editing a new line
    std::string draft_;     // the new line, kept while browsing history
    std::string input_;
    size_t cursor_ = 0;
    int scroll_ = 0;        // rows scrolled back from the newest line
    bool open_ = false;
    float slide_ = 0.0f, blink_ = 0.0f;
    int execDepth_ = 0;
};

// Splits a line into commands on ';' and each command into words, keeping
// "quoted strings" together. Exposed for testing.
std::vector<std::vector<std::string>> tokenizeCommandLine(const std::string& line);

} // namespace sea
