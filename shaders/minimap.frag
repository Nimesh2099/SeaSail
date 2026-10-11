#version 330 core

in vec2 vLocal;

uniform sampler2D uMap;
uniform vec2  uOffset;   // boat position in the texture, in texture widths from its centre
uniform float uScale;    // circle radius in texture widths
uniform int   uHasMap;
uniform float uNight;

out vec4 fragColor;

void main()
{
    float d = length(vLocal);
    if (d > 1.0) discard;   // round map

    // North (-z) is up on screen, east (+x) is right.
    vec2 uv = 0.5 + uOffset + vec2(vLocal.x, -vLocal.y) * uScale;
    vec3 col = vec3(0.06, 0.20, 0.32);   // open-ocean colour while the chart paints
    if (uHasMap == 1 && all(greaterThanEqual(uv, vec2(0.0))) && all(lessThanEqual(uv, vec2(1.0))))
        col = texture(uMap, uv).rgb;

    // Faint range rings at half and full radius, and a soft dark rim.
    float ring = 1.0 - smoothstep(0.0, 0.012, abs(d - 0.5));
    col = mix(col, vec3(1.0), ring * 0.12);
    col *= mix(1.0, 0.55, smoothstep(0.82, 1.0, d));
    // Dim a little at night, like reading a chart by lamplight.
    col *= mix(1.0, 0.75, uNight);

    float alpha = 0.92 * (1.0 - smoothstep(0.985, 1.0, d));
    fragColor = vec4(col, alpha);
}
