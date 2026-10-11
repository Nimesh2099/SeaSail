#version 330 core

// Per vertex (the shared model)
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec4 aColor;       // rgb + emissive
// Per instance
layout(location = 3) in vec4 iPosScale;    // xyz camera-relative, w = scale
layout(location = 4) in vec4 iParams;      // yaw, tint, sway phase, unused

uniform mat4  uViewProj;
uniform float uTime;
uniform float uSway;   // how much this kind of plant moves in the wind
uniform float uFlap;   // gulls: wing beat strength

out vec3 vWorld;
out vec3 vNormal;
out vec4 vColor;

void main()
{
    vec3 p = aPos;

    // Wind: the higher up the model, the further it sways (trunks stay put
    // at the base). Two out-of-step sines per axis so it never looks robotic.
    float phase = iParams.z;
    float bend = uSway * pow(max(p.y, 0.0) / 8.0, 2.0);
    p.x += bend * (0.30 * sin(uTime * 1.1 + phase) + 0.12 * sin(uTime * 2.7 + phase * 3.1));
    p.z += bend * (0.20 * cos(uTime * 0.9 + phase * 1.7));

    // Gulls: wings beat in bursts, then glide.
    float beat = sin(uTime * 9.0 + phase) * smoothstep(0.2, 0.6, sin(uTime * 0.8 + phase * 2.3));
    p.y += uFlap * beat * abs(p.z) * 0.6;

    // Rotate about +Y by the instance's yaw, then scale and place it.
    float c = cos(iParams.x), s = sin(iParams.x);
    mat3 rot = mat3(c, 0.0, -s,
                    0.0, 1.0, 0.0,
                    s, 0.0, c);
    vec3 world = iPosScale.xyz + rot * (p * iPosScale.w);

    vWorld  = world;
    vNormal = rot * aNormal;
    vColor  = vec4(aColor.rgb * iParams.y, aColor.a);
    gl_Position = uViewProj * vec4(world, 1.0);
}
