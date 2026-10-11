// LandmarkModels.h - lighthouse, jetty and fishing hut, built in code,
// plus the rotating lighthouse beam.
#pragma once

#include "Mesh.h"

#include <glad/gl.h>

namespace sea {

class Shader;

constexpr float kLighthouseLampY = 17.6f;  // lamp height above the lighthouse base
constexpr float kHutLanternY     = 2.3f;
constexpr float kJettyLength     = 30.0f;

void buildLighthouse(MeshBuilder& m);
void buildJetty(MeshBuilder& m);   // along +X from the shore, deck at y = 0
void buildHut(MeshBuilder& m);     // door faces +X (toward the sea)

// A sweeping cone of light: two crossed quads that fade along their length
// and toward their edges, drawn additively.
class BeamMesh {
public:
    BeamMesh();
    ~BeamMesh();
    BeamMesh(const BeamMesh&) = delete;
    BeamMesh& operator=(const BeamMesh&) = delete;
    void draw() const;

private:
    GLuint vao_ = 0, vbo_ = 0;
    GLsizei count_ = 0;
};

} // namespace sea
