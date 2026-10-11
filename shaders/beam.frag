#version 330 core

// Lighthouse beam, drawn additively after the sea. The sea's depth is in
// the depth buffer, but islands and the boat are not (they were drawn into
// the off-screen target), so we test against their stored distance here.

in vec3 vWorld;
in vec2 vTS;

uniform vec3  uCamPos;
uniform sampler2D uSceneDistance;
uniform vec2  uScreenSize;
uniform float uIntensity;   // 0 by day

out vec4 fragColor;

void main()
{
    float dist = length(vWorld - uCamPos);
    float scene = texture(uSceneDistance, gl_FragCoord.xy / uScreenSize).r;
    if (scene < dist) discard;

    // Brightest at the lamp and along the beam's axis, soft at the edges.
    float along  = pow(1.0 - vTS.x, 1.6);
    float across = 1.0 - smoothstep(0.0, 1.0, abs(vTS.y));
    float a = along * across * across * uIntensity;
    // Fade where the beam gets close to the camera, so flying through it
    // doesn't white out the screen.
    a *= smoothstep(2.0, 25.0, dist);
    fragColor = vec4(vec3(1.0, 0.92, 0.72) * a * 0.35, 1.0);
}
