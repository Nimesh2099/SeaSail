#version 330 core

in vec2 vUv;
in vec4 vColor;

uniform sampler2D uFont;   // one channel: 1 where the glyph has a pixel

out vec4 fragColor;

void main()
{
    float a = texture(uFont, vUv).r;
    if (a < 0.5) discard;
    fragColor = vec4(vColor.rgb, vColor.a);
}
