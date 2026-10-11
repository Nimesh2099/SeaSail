// Ocean.h - the endless sea: Gerstner waves, a camera-following mesh, and
// the same waves evaluated on the CPU so the boat floats on exactly the
// surface you see.
#pragma once

#include "SeaMath.h"

#include <glad/gl.h>
#include <vector>

namespace sea {

class Shader;

struct Wave {
    float dirX, dirZ;   // unit direction of travel
    float wavelength;   // metres
    float amplitude;    // metres
    float steepness;    // Q*k*A: 0 = sine wave, 1 = sharpest crest
    bool  displaces;    // false = per-pixel ripple only
};

struct SurfacePoint {
    float height;   // metres above sea level
    Vec3  normal;
};

class Ocean {
public:
    static constexpr int kMaxWaves = 10;

    Ocean();
    ~Ocean();
    Ocean(const Ocean&) = delete;
    Ocean& operator=(const Ocean&) = delete;

    // Wave uniforms for the shaders. Phases are computed in double from the
    // absolute world origin and time, so waves stay continuous anywhere.
    void setUniforms(const Shader& s, double originX, double originZ, double time) const;
    void draw() const;

    // The surface at a world position, matching the GPU waves. Gerstner
    // waves also move water sideways, so we first solve for which
    // undisplaced point ends up at (x, z), then read its height.
    SurfacePoint sample(double x, double z, double time) const;

    size_t vertexCount() const { return vertexCount_; }

    // Scale every wave's height (1 = normal, 0 = flat calm, 2 = rough).
    // The CPU surface uses the same waves, so the boat follows along.
    void  setWaveScale(float scale);
    float waveScale() const { return waveScale_; }

private:
    void displacement(double x, double z, double time, double& dx, double& dy, double& dz) const;

    std::vector<Wave> waves_;
    std::vector<Wave> baseWaves_;  // as designed, before scaling
    float waveScale_ = 1.0f;
    GLuint vao_ = 0, vbo_ = 0, ebo_ = 0;
    GLsizei indexCount_ = 0;
    size_t vertexCount_ = 0;
};

} // namespace sea
