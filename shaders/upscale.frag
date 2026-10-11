#version 330 core

// Stretches the 3D view, rendered below screen resolution, over the whole
// screen. Bilinear filtering softens it, so a light sharpen (the pixel
// minus a little of its neighbours' average) brings the detail back.

in vec2 vNdc;

uniform sampler2D uSource;
uniform vec2  uSourceSize;   // pixels
uniform float uSharpen;      // 0 = plain bilinear

out vec4 fragColor;

void main()
{
    vec2 uv = vNdc * 0.5 + 0.5;
    vec2 px = 1.0 / uSourceSize;
    vec3 c = texture(uSource, uv).rgb;
    vec3 n = texture(uSource, uv + vec2(px.x, 0.0)).rgb + texture(uSource, uv - vec2(px.x, 0.0)).rgb
           + texture(uSource, uv + vec2(0.0, px.y)).rgb + texture(uSource, uv - vec2(0.0, px.y)).rgb;
    fragColor = vec4(clamp(c + (c - n * 0.25) * uSharpen, 0.0, 1.0), 1.0);
}
