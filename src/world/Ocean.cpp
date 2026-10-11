#include "Ocean.h"

#include "Shader.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace sea {

namespace {

constexpr double kTwoPi = 6.283185307179586;
constexpr double kGravity = 9.81;

// Deep-water dispersion: w = sqrt(g k). Long swells outrun short chop.
double omega(double k) { return std::sqrt(kGravity * k); }

} // namespace

Ocean::Ocean()
{
    // A moderate swell from one wind direction. The last four waves are
    // too small to move vertices; they only ripple the lighting.
    const float wind = 0.6f;
    struct Def { float wavelength, amplitude, angle, steepness; bool displaces; };
    const Def defs[] = {
        {95.0f, 0.50f,  0.00f, 0.12f, true},
        {61.0f, 0.33f,  0.45f, 0.12f, true},
        {37.0f, 0.20f, -0.60f, 0.12f, true},
        {23.0f, 0.11f,  0.90f, 0.11f, true},
        {13.0f, 0.06f, -0.30f, 0.10f, true},
        { 7.5f, 0.03f,  1.30f, 0.09f, true},
        { 3.1f, 0.012f, -1.0f, 0.0f, false},
        { 1.9f, 0.008f, 2.10f, 0.0f, false},
        { 1.2f, 0.005f, 0.30f, 0.0f, false},
        { 0.7f, 0.003f, -2.4f, 0.0f, false},
    };
    for (const Def& d : defs) {
        float a = wind + d.angle;
        waves_.push_back({std::cos(a), std::sin(a), d.wavelength, d.amplitude, d.steepness, d.displaces});
    }
    baseWaves_ = waves_;

    // Disc of rings around the camera: 25 cm apart at the centre, growing
    // 3.5% per ring out to 20 km. It moves with the camera every frame.
    std::vector<float> radii;
    for (int i = 0; i <= 40; ++i) radii.push_back(i * 0.25f);
    while (radii.back() < 20000.0f) radii.push_back(radii.back() * 1.035f);

    const int segments = 192;
    std::vector<float> verts;
    for (float r : radii)
        for (int s = 0; s <= segments; ++s) {
            float a = static_cast<float>(s) / segments * 2.0f * kPi;
            verts.push_back(std::cos(a) * r);
            verts.push_back(std::sin(a) * r);
        }
    std::vector<uint32_t> idx;
    const uint32_t row = segments + 1;
    for (uint32_t ring = 0; ring + 1 < radii.size(); ++ring)
        for (uint32_t s = 0; s < static_cast<uint32_t>(segments); ++s) {
            uint32_t a = ring * row + s, b = a + 1, c = a + row, d = c + 1;
            idx.insert(idx.end(), {a, c, b, b, c, d});
        }

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glGenBuffers(1, &ebo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(verts.size() * sizeof(float)), verts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(idx.size() * sizeof(uint32_t)), idx.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glBindVertexArray(0);
    indexCount_ = static_cast<GLsizei>(idx.size());
    vertexCount_ = verts.size() / 2;
}

Ocean::~Ocean()
{
    glDeleteVertexArrays(1, &vao_);
    glDeleteBuffers(1, &vbo_);
    glDeleteBuffers(1, &ebo_);
}

void Ocean::setUniforms(const Shader& s, double originX, double originZ, double time) const
{
    // Two vec4 per wave:  A = (dir.x, dir.z, k, amplitude)
    //                     B = (Q, phase, fade distance, displaces)
    // and the shader computes theta = k * dot(dir, localPos) + phase.
    float a[kMaxWaves * 4] = {}, b[kMaxWaves * 4] = {};
    int n = 0;
    for (const Wave& w : waves_) {
        if (n == kMaxWaves) break;
        double k = kTwoPi / w.wavelength;
        // Origin and time grow without bound, so fold them into the phase
        // here in double and wrap to [0, 2pi).
        double phase = std::fmod(k * (w.dirX * originX + w.dirZ * originZ) - omega(k) * time, kTwoPi);
        float q = w.steepness > 0.0f ? w.steepness / static_cast<float>(k * w.amplitude) : 0.0f;
        a[n * 4 + 0] = w.dirX;
        a[n * 4 + 1] = w.dirZ;
        a[n * 4 + 2] = static_cast<float>(k);
        a[n * 4 + 3] = w.amplitude;
        b[n * 4 + 0] = q;
        b[n * 4 + 1] = static_cast<float>(phase);
        b[n * 4 + 2] = w.wavelength * 80.0f; // fade where a wave gets smaller than a pixel
        b[n * 4 + 3] = w.displaces ? 1.0f : 0.0f;
        ++n;
    }
    s.setVec4Array("uWaveA", a, n);
    s.setVec4Array("uWaveB", b, n);
    s.set("uWaveCount", n);
}

void Ocean::draw() const
{
    glBindVertexArray(vao_);
    glDrawElements(GL_TRIANGLES, indexCount_, GL_UNSIGNED_INT, nullptr);
}

void Ocean::displacement(double x, double z, double time, double& dx, double& dy, double& dz) const
{
    dx = dy = dz = 0.0;
    for (const Wave& w : waves_) {
        if (!w.displaces) continue;
        double k = kTwoPi / w.wavelength;
        double q = w.steepness / (k * w.amplitude);
        double theta = k * (w.dirX * x + w.dirZ * z) - omega(k) * time;
        double c = std::cos(theta), s = std::sin(theta);
        dx += q * w.amplitude * w.dirX * c;
        dz += q * w.amplitude * w.dirZ * c;
        dy += w.amplitude * s;
    }
}

SurfacePoint Ocean::sample(double x, double z, double time) const
{
    // Find the undisplaced point p with p + D(p) = (x, z). The sideways
    // shift is small and smooth, so fixed-point iteration converges fast.
    double px = x, pz = z, dx, dy, dz;
    for (int i = 0; i < 4; ++i) {
        displacement(px, pz, time, dx, dy, dz);
        px = x - dx;
        pz = z - dz;
    }
    displacement(px, pz, time, dx, dy, dz);

    // Analytic normal of the Gerstner surface at p.
    double nx = 0.0, ny = 1.0, nz = 0.0;
    for (const Wave& w : waves_) {
        if (!w.displaces) continue;
        double k = kTwoPi / w.wavelength;
        double q = w.steepness / (k * w.amplitude);
        double theta = k * (w.dirX * px + w.dirZ * pz) - omega(k) * time;
        double kA = k * w.amplitude;
        nx -= w.dirX * kA * std::cos(theta);
        nz -= w.dirZ * kA * std::cos(theta);
        ny -= q * kA * std::sin(theta);
    }
    return {static_cast<float>(dy), normalize(Vec3{static_cast<float>(nx), static_cast<float>(ny), static_cast<float>(nz)})};
}

void Ocean::setWaveScale(float scale)
{
    // Never exactly zero: the Gerstner maths divides by amplitude.
    waveScale_ = clampf(scale, 0.0f, 3.0f);
    const float sc = std::max(waveScale_, 0.001f);
    // Steepness is Q*k*A, so it grows with height. Once the waves' total
    // passes ~0.9 the crests fold over themselves (and the CPU surface solve
    // stops converging), so past that point waves get taller, not sharper.
    float total = 0.0f;
    for (const Wave& w : baseWaves_) total += w.steepness;
    const float steepSc = total > 0.0f ? std::min(sc, 0.9f / total) : sc;
    for (size_t i = 0; i < waves_.size(); ++i) {
        waves_[i].amplitude = baseWaves_[i].amplitude * sc;
        waves_[i].steepness = baseWaves_[i].steepness * steepSc;
    }
}

} // namespace sea
