#version 330 core

layout(location = 0) in vec2 aPos;    // pixels, (0,0) = bottom-left
layout(location = 1) in vec4 aColor;

uniform vec2 uScreen;

out vec4 vColor;

void main()
{
    vColor = aColor;
    gl_Position = vec4(aPos / uScreen * 2.0 - 1.0, 0.0, 1.0);
}
