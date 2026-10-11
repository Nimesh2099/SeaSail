#include "Shader.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <vector>

namespace sea {

namespace {

bool readFile(const std::string& path, std::string& out)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

// GLSL has no #include, so we do a tiny one ourselves: a line of the form
//   #include "file.glsl"
// is replaced by that file's contents (one level deep is all we need).
std::string expandIncludes(const std::string& src)
{
    std::istringstream in(src);
    std::string line, out;
    while (std::getline(in, line)) {
        if (line.rfind("#include", 0) == 0) {
            size_t a = line.find('"'), b = line.rfind('"');
            std::string inc;
            if (a != std::string::npos && b > a && readFile(findShaderFile(line.substr(a + 1, b - a - 1)), inc)) {
                out += inc + "\n";
                continue;
            }
            std::fprintf(stderr, "[shader] bad include: %s\n", line.c_str());
        }
        out += line + "\n";
    }
    return out;
}

GLuint compile(GLenum type, const std::string& src, const std::string& path)
{
    GLuint s = glCreateShader(type);
    const char* c = src.c_str();
    glShaderSource(s, 1, &c, nullptr);
    glCompileShader(s);

    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint len = 0;
        glGetShaderiv(s, GL_INFO_LOG_LENGTH, &len);
        std::vector<char> log(static_cast<size_t>(len > 1 ? len : 1));
        glGetShaderInfoLog(s, len, nullptr, log.data());
        std::fprintf(stderr, "[shader] %s failed to compile:\n%s\n", path.c_str(), log.data());
        glDeleteShader(s);
        return 0;
    }
    return s;
}

} // namespace

std::string findShaderFile(const std::string& name)
{
    const char* prefixes[] = {"shaders/", "../shaders/", "../../shaders/", "../../../shaders/"};
    for (const char* p : prefixes) {
        std::string path = std::string(p) + name;
        if (std::ifstream(path)) return path;
    }
    return std::string("shaders/") + name;
}

Shader::Shader(std::string vertPath, std::string fragPath)
    : vertPath_(std::move(vertPath)), fragPath_(std::move(fragPath))
{
    reload();
}

Shader::~Shader()
{
    if (program_) glDeleteProgram(program_);
}

bool Shader::reload()
{
    std::string vsrc, fsrc;
    if (!readFile(vertPath_, vsrc) || !readFile(fragPath_, fsrc)) {
        std::fprintf(stderr, "[shader] could not read %s / %s\n", vertPath_.c_str(), fragPath_.c_str());
        return false;
    }
    vsrc = expandIncludes(vsrc);
    fsrc = expandIncludes(fsrc);

    GLuint vs = compile(GL_VERTEX_SHADER, vsrc, vertPath_);
    GLuint fs = compile(GL_FRAGMENT_SHADER, fsrc, fragPath_);
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return false;
    }

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
        std::fprintf(stderr, "[shader] link failed:\n%s\n", log);
        glDeleteProgram(prog);
        return false;
    }

    if (program_) glDeleteProgram(program_);
    program_ = prog;
    return true;
}

void Shader::set(const char* name, float v) const { glUniform1f(loc(name), v); }
void Shader::set(const char* name, int v) const { glUniform1i(loc(name), v); }
void Shader::set(const char* name, const Vec3& v) const { glUniform3f(loc(name), v.x, v.y, v.z); }
void Shader::set(const char* name, const Mat4& m) const { glUniformMatrix4fv(loc(name), 1, GL_FALSE, m.m); }
void Shader::setVec2(const char* name, float x, float y) const { glUniform2f(loc(name), x, y); }
void Shader::setVec4Array(const char* name, const float* xyzw, int count) const
{
    glUniform4fv(loc(name), count, xyzw);
}

} // namespace sea
