// SceneTarget.h - off-screen render target for everything except the water.
//
// Two colour attachments:
//   0: RGBA16F  HDR scene colour (sky + terrain, before tone mapping)
//   1: R32F     distance from the camera to the surface at each pixel
// The water shader reads both: the colour is what you see *through* the
// water, and the distance tells it how much water the light crossed.
#pragma once

#include <glad/gl.h>

namespace sea {

class SceneTarget {
public:
    ~SceneTarget() { destroy(); }

    // (Re)creates the textures if the size changed. Returns false on failure.
    bool resize(int w, int h);
    void bind() const;

    GLuint colorTex() const { return color_; }
    GLuint distanceTex() const { return distance_; }
    int width() const { return w_; }
    int height() const { return h_; }

private:
    void destroy();

    GLuint fbo_ = 0, color_ = 0, distance_ = 0, depth_ = 0;
    int w_ = 0, h_ = 0;
};

} // namespace sea
