#include "Console.h"

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <fstream>

namespace sea {

namespace {

constexpr size_t kMaxLines   = 500;
constexpr size_t kMaxHistory = 100;

bool startsWith(const std::string& s, const std::string& prefix)
{
    if (prefix.size() > s.size()) return false;
    for (size_t i = 0; i < prefix.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(s[i])) != std::tolower(static_cast<unsigned char>(prefix[i]))) return false;
    return true;
}

bool isWordChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.' || c == '-'; }

} // namespace

std::vector<std::vector<std::string>> tokenizeCommandLine(const std::string& line)
{
    std::vector<std::vector<std::string>> out(1);
    std::string word;
    bool inQuotes = false, haveWord = false;
    auto endWord = [&] {
        if (haveWord) out.back().push_back(word);
        word.clear();
        haveWord = false;
    };
    for (char c : line) {
        if (inQuotes) {
            if (c == '"') inQuotes = false;
            else word += c;
        } else if (c == '"') {
            inQuotes = true;
            haveWord = true;  // "" is a real (empty) argument
        } else if (c == ';') {
            endWord();
            out.emplace_back();
        } else if (std::isspace(static_cast<unsigned char>(c))) {
            endWord();
        } else {
            word += c;
            haveWord = true;
        }
    }
    endWord();
    out.erase(std::remove_if(out.begin(), out.end(), [](const auto& v) { return v.empty(); }), out.end());
    return out;
}

Console::Console() = default;

std::string Console::lower(std::string s)
{
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

void Console::add(ConsoleCommand cmd)
{
    cmd.name = lower(cmd.name);
    commands_.push_back(std::move(cmd));
    std::sort(commands_.begin(), commands_.end(), [](const auto& a, const auto& b) { return a.name < b.name; });
}

const ConsoleCommand* Console::find(const std::string& name) const
{
    std::string n = lower(name);
    for (const ConsoleCommand& c : commands_)
        if (c.name == n) return &c;
    return nullptr;
}

// ------------------------------------------------------------------ output ---

void Console::log(LineKind kind, const char* fmt, va_list args)
{
    char buf[1024];
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    std::printf("%s%s\n", kind == LineKind::Error ? "error: " : "", buf);

    // One log entry per line of text.
    std::string s(buf);
    size_t start = 0;
    while (true) {
        size_t nl = s.find('\n', start);
        lines_.push_back({s.substr(start, nl == std::string::npos ? std::string::npos : nl - start), kind});
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    while (lines_.size() > kMaxLines) lines_.pop_front();
    // Stay where the reader is if they've scrolled back.
    if (scroll_ > 0) ++scroll_;
}

void Console::print(const char* fmt, ...)
{
    va_list a;
    va_start(a, fmt);
    log(LineKind::Info, fmt, a);
    va_end(a);
}

void Console::error(const char* fmt, ...)
{
    va_list a;
    va_start(a, fmt);
    log(LineKind::Error, fmt, a);
    va_end(a);
}

void Console::heading(const char* fmt, ...)
{
    va_list a;
    va_start(a, fmt);
    log(LineKind::Heading, fmt, a);
    va_end(a);
}

// --------------------------------------------------------------- execution ---

void Console::execute(const std::string& line, bool echo)
{
    if (echo) {
        lines_.push_back({"> " + line, LineKind::Echo});
        std::printf("> %s\n", line.c_str());
    }
    for (const auto& tokens : tokenizeCommandLine(line)) runOne(tokens);
}

void Console::runOne(const std::vector<std::string>& tokens)
{
    const ConsoleCommand* cmd = find(tokens[0]);
    if (!cmd) {
        error("Unknown command '%s'. Type 'help' for a list.", tokens[0].c_str());
        return;
    }
    cmd->run(ConsoleArgs(tokens.begin() + 1, tokens.end()));
}

bool Console::execFile(const std::string& path)
{
    std::ifstream in(path);
    if (!in) return false;
    if (execDepth_ >= 8) {
        error("exec: too deeply nested (does %s exec itself?)", path.c_str());
        return true;
    }
    ++execDepth_;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();  // Windows line endings
        size_t first = line.find_first_not_of(" \t");
        if (first == std::string::npos || line[first] == '#' || line.compare(first, 2, "//") == 0) continue;
        execute(line.substr(first));
    }
    --execDepth_;
    return true;
}

// ------------------------------------------------------------------- input ---

void Console::setOpen(bool open)
{
    open_ = open;
    blink_ = 0.0f;
}

void Console::update(float dt)
{
    // Slides down in about a tenth of a second.
    float target = open_ ? 1.0f : 0.0f;
    float step = dt * 9.0f;
    slide_ = slide_ < target ? std::min(target, slide_ + step) : std::max(target, slide_ - step);
    blink_ += dt;
    if (blink_ >= 1.0f) blink_ -= 1.0f;
}

void Console::insertText(const std::string& s)
{
    for (char c : s)
        if (c >= 32 && c <= 126) onChar(static_cast<unsigned char>(c));
}

void Console::onChar(unsigned int cp)
{
    if (!open_) return;
    if (cp == '`' || cp == '~') return;   // the toggle key, not text
    if (cp < 32 || cp > 126) return;       // the font is plain ASCII
    if (input_.size() >= 200) return;
    input_.insert(cursor_, 1, static_cast<char>(cp));
    ++cursor_;
    blink_ = 0.0f;
}

void Console::scrollBy(int rows) { scroll_ = std::max(0, scroll_ + rows); }

void Console::clampScroll(int maxRows) { scroll_ = std::clamp(scroll_, 0, std::max(0, maxRows)); }

void Console::onKey(ConsoleKey key, bool ctrl)
{
    if (!open_) return;
    blink_ = 0.0f;
    switch (key) {
    case ConsoleKey::Enter: {
        std::string line = input_;
        input_.clear();
        cursor_ = 0;
        historyPos_ = -1;
        scroll_ = 0;
        if (line.find_first_not_of(' ') == std::string::npos) break;
        if (history_.empty() || history_.back() != line) history_.push_back(line);
        if (history_.size() > kMaxHistory) history_.erase(history_.begin());
        execute(line);
        break;
    }
    case ConsoleKey::Backspace:
        if (cursor_ == 0) break;
        if (ctrl) {  // delete the previous word
            size_t p = cursor_;
            while (p > 0 && input_[p - 1] == ' ') --p;
            while (p > 0 && input_[p - 1] != ' ') --p;
            input_.erase(p, cursor_ - p);
            cursor_ = p;
        } else {
            input_.erase(--cursor_, 1);
        }
        break;
    case ConsoleKey::Delete:
        if (cursor_ < input_.size()) input_.erase(cursor_, 1);
        break;
    case ConsoleKey::Left:
        if (ctrl) {
            while (cursor_ > 0 && !isWordChar(input_[cursor_ - 1])) --cursor_;
            while (cursor_ > 0 && isWordChar(input_[cursor_ - 1])) --cursor_;
        } else if (cursor_ > 0) {
            --cursor_;
        }
        break;
    case ConsoleKey::Right:
        if (ctrl) {
            while (cursor_ < input_.size() && !isWordChar(input_[cursor_])) ++cursor_;
            while (cursor_ < input_.size() && isWordChar(input_[cursor_])) ++cursor_;
        } else if (cursor_ < input_.size()) {
            ++cursor_;
        }
        break;
    case ConsoleKey::Home: cursor_ = 0; break;
    case ConsoleKey::End:  cursor_ = input_.size(); break;
    case ConsoleKey::Up:
        if (history_.empty()) break;
        if (historyPos_ < 0) {
            draft_ = input_;
            historyPos_ = static_cast<int>(history_.size()) - 1;
        } else if (historyPos_ > 0) {
            --historyPos_;
        }
        input_ = history_[static_cast<size_t>(historyPos_)];
        cursor_ = input_.size();
        break;
    case ConsoleKey::Down:
        if (historyPos_ < 0) break;
        if (++historyPos_ >= static_cast<int>(history_.size())) {
            historyPos_ = -1;
            input_ = draft_;
        } else {
            input_ = history_[static_cast<size_t>(historyPos_)];
        }
        cursor_ = input_.size();
        break;
    case ConsoleKey::Tab:      complete(); break;
    case ConsoleKey::PageUp:   scrollBy(ctrl ? 1000000 : 8); break;
    case ConsoleKey::PageDown: scrollBy(ctrl ? -1000000 : -8); break;
    }
}

// Tab: complete the word under the cursor from command names (first word)
// or from the command's options (second word). Several matches complete as
// far as they agree; the hint list under the input shows the rest.
void Console::complete()
{
    // Only the current command after the last ';' matters.
    size_t cmdStart = input_.rfind(';', cursor_ == 0 ? 0 : cursor_ - 1);
    cmdStart = cmdStart == std::string::npos ? 0 : cmdStart + 1;
    while (cmdStart < cursor_ && input_[cmdStart] == ' ') ++cmdStart;
    size_t wordStart = input_.rfind(' ', cursor_ == 0 ? 0 : cursor_ - 1);
    wordStart = (wordStart == std::string::npos || wordStart < cmdStart) ? cmdStart : wordStart + 1;
    if (cursor_ > 0 && input_[cursor_ - 1] == ' ') wordStart = cursor_;
    std::string word = input_.substr(wordStart, cursor_ - wordStart);

    std::vector<std::string> pool;
    if (wordStart == cmdStart) {
        for (const ConsoleCommand& c : commands_) pool.push_back(c.name);
    } else {
        // Second word only: the first argument is the one with known options.
        std::string before = input_.substr(cmdStart, wordStart - cmdStart);
        auto toks = tokenizeCommandLine(before);
        if (toks.size() != 1 || toks[0].size() != 1) return;
        const ConsoleCommand* cmd = find(toks[0][0]);
        if (!cmd) return;
        pool = cmd->options;
    }
    std::vector<std::string> matches;
    for (const std::string& p : pool)
        if (startsWith(p, word)) matches.push_back(p);
    if (matches.empty()) return;

    std::string common = matches[0];
    for (const std::string& m : matches) {
        size_t n = 0;
        while (n < common.size() && n < m.size() && std::tolower(static_cast<unsigned char>(common[n])) ==
                                                         std::tolower(static_cast<unsigned char>(m[n])))
            ++n;
        common.resize(n);
    }
    if (matches.size() == 1) common += ' ';
    input_.replace(wordStart, cursor_ - wordStart, common);
    cursor_ = wordStart + common.size();
}

std::vector<std::string> Console::hints(size_t max) const
{
    std::vector<std::string> out;
    size_t semi = input_.rfind(';');
    std::string cur = input_.substr(semi == std::string::npos ? 0 : semi + 1);
    size_t first = cur.find_first_not_of(' ');
    if (first == std::string::npos) return out;
    cur = cur.substr(first);
    size_t space = cur.find(' ');

    auto describe = [](const ConsoleCommand& c) {
        std::string s = c.name;
        if (!c.usage.empty()) s += " " + c.usage;
        if (s.size() < 26) s.resize(26, ' ');
        return s + "  " + c.help;
    };
    if (space == std::string::npos) {
        // Still typing the command name: list what it could become.
        for (const ConsoleCommand& c : commands_) {
            if (!startsWith(c.name, cur)) continue;
            if (out.size() == max) {
                out.back() = "...";
                break;
            }
            out.push_back(describe(c));
        }
    } else if (const ConsoleCommand* c = find(cur.substr(0, space))) {
        out.push_back(describe(*c));
        if (!c->options.empty()) {
            std::string opts = "  options:";
            for (const std::string& o : c->options) opts += " " + o;
            out.push_back(opts);
        }
    }
    return out;
}

// -------------------------------------------------------- argument helpers ---

bool Console::number(const std::string& s, double& out, const char* what)
{
    char* end = nullptr;
    double v = std::strtod(s.c_str(), &end);
    if (s.empty() || *end != '\0' || v != v) {
        error("%s: '%s' is not a number", what, s.c_str());
        return false;
    }
    out = v;
    return true;
}

bool Console::number(const std::string& s, float& out, const char* what)
{
    double d;
    if (!number(s, d, what)) return false;
    out = static_cast<float>(d);
    return true;
}

bool Console::onOff(const ConsoleArgs& args, bool current, bool& out)
{
    if (args.empty()) {
        out = !current;
        return true;
    }
    std::string a = lower(args[0]);
    if (a == "1" || a == "on" || a == "true" || a == "yes") { out = true; return true; }
    if (a == "0" || a == "off" || a == "false" || a == "no") { out = false; return true; }
    if (a == "toggle") { out = !current; return true; }
    error("expected on or off, got '%s'", args[0].c_str());
    return false;
}

} // namespace sea
