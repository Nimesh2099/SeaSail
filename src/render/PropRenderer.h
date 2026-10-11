// PropRenderer.h - draws thousands of trees and rocks with instancing:
// one draw call per prop type, however many are on screen.
#pragma once

#include "Mesh.h"
#include "Props.h"

#include <glad/gl.h>

namespace sea {

class Shader;

class PropRenderer {
public:
    PropRenderer();
    ~PropRenderer();
    PropRenderer(const PropRenderer&) = delete;
    PropRenderer& operator=(const PropRenderer&) = delete;

    // `lists` hold camera-relative positions (from TerrainStreamer::gatherProps).
    void draw(const Shader& shader, const PropLists& lists);

private:
    struct Kind {
        Mesh   mesh;           // the model, shared by every instance
        GLuint vao = 0;        // model attributes + per-instance attributes
        GLuint instanceVbo = 0;
        float  sway = 0.0f;    // how much the wind moves it
        float  flap = 0.0f;    // wing flapping (gulls)
    };
    Kind kinds_[kPropTypes];
};

// The models themselves (also used for the README's close-ups).
void buildPalm(MeshBuilder& m);
void buildBroadleaf(MeshBuilder& m);
void buildPine(MeshBuilder& m);
void buildBush(MeshBuilder& m);
void buildRock(MeshBuilder& m);
void buildGull(MeshBuilder& m);

} // namespace sea
