#version 330 core

// A screen-space square around the minimap circle, from gl_VertexID.
uniform vec2  uScreen;
uniform vec2  uCentre;   // pixels, (0,0) = bottom-left
uniform float uRadius;   // pixels

out vec2 vLocal;         // -1..1 across the square

void main()
{
    const vec2 corners[6] = vec2[6](vec2(-1, -1), vec2(1, -1), vec2(1, 1),
                                    vec2(-1, -1), vec2(1, 1), vec2(-1, 1));
    vLocal = corners[gl_VertexID];
    vec2 px = uCentre + vLocal * uRadius;
    gl_Position = vec4(px / uScreen * 2.0 - 1.0, 0.0, 1.0);
}
