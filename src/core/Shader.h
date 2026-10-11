// Shader.h - loads, compiles and links a GLSL program from two files.
// Supports hot reload: edit a .glsl file, press R, see the change.
#pragma once

#include "SeaMath.h"
#include <glad/gl.h>
#include <string>

namespace sea {

class Shader {
public:
    Shader() = default;
    Shader(std::string vertPath, std::string fragPath);
    ~Shader();
    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;

    // Rebuild from disk. On failure the previous program is kept and the
    // compiler log is printed, so a typo never kills the running demo.
    bool reload();
    void use() const { glUseProgram(program_); }
    bool valid() const { return program_ != 0; }

    void set(const char* name, float v) const;
    void set(const char* name, int v) const;
    void set(const char* name, const Vec3& v) const;
    void set(const char* name, const Mat4& m) const;
    void setVec2(const char* name, float x, float y) const;
    void setVec4Array(const char* name, const float* xyzw, int count) const;

private:
    GLint loc(const char* name) const { return glGetUniformLocation(program_, name); }

    std::string vertPath_, fragPath_;
    GLuint program_ = 0;
};

// Finds a file in "shaders/" next to the working dir or a few levels up,
// so the demo runs from the build folder, build/Release, or the repo root.
std::string findShaderFile(const std::string& name);

} // namespace sea
