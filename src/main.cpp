// SeaSail - an endless ocean with procedurally generated islands, explored
// by boat. Entry point: window, input, game loop and render passes.
//
// Controls
//   W / S        throttle ahead / astern       A / D     steer
//   Shift        boost                         Mouse     orbit the camera
//   Wheel        zoom                          Tab       release / capture the mouse
//   [ / ]        time of day back / forward    T         pause / resume the clock
//   L            boat lights on / off          V         free camera on/off
//   Backspace    respawn by an island          N         new world (new seed)
//   H            hide HUD    F  wireframe      R         reload shaders
//   P            screenshot (.bmp)             Esc       quit
//
// Command line (handy for testing and README images)
//   SeaSail [--seed N] [--radius CHUNKS] [--width W --height H] [--hour 0..24]
//           [--view chase|side|high|low|cove] [--simulate SECONDS] [--steer -1..1]
//           [--island mountain|atoll|stacks|cay] [--time SECONDS] [--shot out.bmp]

#include "Boat.h"
#include "BoatModel.h"
#include "Camera.h"
#include "ChaseCamera.h"
#include "Environment.h"
#include "Frustum.h"
#include "Hud.h"
#include "IslandGenerator.h"
#include "LandmarkModels.h"
#include "Landmarks.h"
#include "Mesh.h"
#include "Ocean.h"
#include "PropRenderer.h"
#include "SceneTarget.h"
#include "Screenshot.h"
#include "Shader.h"
#include "TerrainStreamer.h"

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace sea;

namespace {

// ------------------------------------------------------------------ options --

struct Options {
    uint32_t    seed     = 2026;
    int         radius   = 36;     // terrain view radius in 128 m chunks (~4.6 km)
    int         width    = 1280;
    int         height   = 720;
    std::string view     = "chase";
    std::string shot;              // render one frame to this file and exit
    double      simulate = 0.0;    // seconds of autopilot before the shot
    float       steer    = 0.0f;   // autopilot steering
    double      time     = 30.0;   // start time (waves, caustics)
    float       hour     = 10.0f;  // start time of day
    IslandType  island   = IslandType::Mountain;  // kind of island to start next to
};

Options parseArgs(int argc, char** argv)
{
    Options o;
    for (int i = 1; i < argc; ++i) {
        auto next = [&](void) -> const char* { return i + 1 < argc ? argv[++i] : ""; };
        if      (!std::strcmp(argv[i], "--seed"))     o.seed     = static_cast<uint32_t>(std::strtoul(next(), nullptr, 10));
        else if (!std::strcmp(argv[i], "--radius"))   o.radius   = std::atoi(next());
        else if (!std::strcmp(argv[i], "--width"))    o.width    = std::atoi(next());
        else if (!std::strcmp(argv[i], "--height"))   o.height   = std::atoi(next());
        else if (!std::strcmp(argv[i], "--view"))     o.view     = next();
        else if (!std::strcmp(argv[i], "--shot"))     o.shot     = next();
        else if (!std::strcmp(argv[i], "--simulate")) o.simulate = std::atof(next());
        else if (!std::strcmp(argv[i], "--steer"))    o.steer    = static_cast<float>(std::atof(next()));
        else if (!std::strcmp(argv[i], "--time"))     o.time     = std::atof(next());
        else if (!std::strcmp(argv[i], "--hour"))     o.hour     = static_cast<float>(std::atof(next()));
        else if (!std::strcmp(argv[i], "--island")) {
            std::string t = next();
            o.island = t == "atoll" ? IslandType::Atoll : t == "stacks" ? IslandType::SeaStacks
                     : t == "cay" ? IslandType::Cay : IslandType::Mountain;
        }
    }
    o.radius = std::max(8, std::min(o.radius, 64));
    return o;
}

// --------------------------------------------------------------------- app ---

struct Lights {
    float pos[6 * 4] = {};
    float col[6 * 4] = {};
    int   count = 0;
    Vec3  spotPos, spotDir, spotColor;

    void add(const Vec3& p, float range, const Vec3& c)
    {
        if (count == 6) return;
        pos[count * 4 + 0] = p.x; pos[count * 4 + 1] = p.y; pos[count * 4 + 2] = p.z; pos[count * 4 + 3] = range;
        col[count * 4 + 0] = c.x; col[count * 4 + 1] = c.y; col[count * 4 + 2] = c.z;
        ++count;
    }
};

struct App {
    Options     opt;
    GLFWwindow* window = nullptr;

    std::unique_ptr<IslandGenerator> world;
    std::unique_ptr<TerrainStreamer> terrain;
    std::unique_ptr<Ocean>           ocean;
    std::unique_ptr<SceneTarget>     scene;
    std::unique_ptr<Mesh>            boatMesh, lighthouseMesh, jettyMesh, hutMesh;
    std::unique_ptr<BeamMesh>        beamMesh;
    std::unique_ptr<PropRenderer>    props;
    std::unique_ptr<Hud>             hud;
    std::unique_ptr<Shader> skyShader, terrainShader, objectShader, propShader, waterShader, compositeShader,
                            beamShader, hudShader;
    GLuint emptyVao = 0;

    Landmarks   landmarks;
    PropLists   visibleProps;
    Boat        boat;
    ChaseCamera chase;
    Camera      cam;
    Environment env;
    Lights      lights;
    bool        freeCam = false;
    bool        boatLights = true;

    // Floating origin: everything the GPU sees is relative to this point,
    // which hops in 1 km steps to stay near the player.
    double originX = 0.0, originZ = 0.0;
    double time = 0.0;

    bool   mouseCaptured = false, firstMouse = true;
    double lastMouseX = 0.0, lastMouseY = 0.0;
    float  scrollAccum = 0.0f;
    bool   showHud = true, wireframe = false;
    int    screenshotCount = 0;

    std::vector<Shader*> allShaders() const
    {
        return {skyShader.get(), terrainShader.get(), objectShader.get(), propShader.get(), waterShader.get(),
                compositeShader.get(), beamShader.get(), hudShader.get()};
    }
};

App* g_app = nullptr;
constexpr double kRebaseStep = 1024.0;
constexpr float  kPropFadeStart = 1100.0f, kPropFadeEnd = 1450.0f;

Vec3 boatLocal(const App& a)
{
    return {static_cast<float>(a.boat.x() - a.originX), a.boat.y(), static_cast<float>(a.boat.z() - a.originZ)};
}

double camWorldX(const App& a) { return a.originX + a.cam.position.x; }
double camWorldZ(const App& a) { return a.originZ + a.cam.position.z; }

Vec3 toLocal(const App& a, double x, float y, double z)
{
    return {static_cast<float>(x - a.originX), y, static_cast<float>(z - a.originZ)};
}

// Move the origin so the point (worldX, worldZ) stays within one step of it.
void rebaseOrigin(App& a, double focusX, double focusZ)
{
    double nx = std::floor(focusX / kRebaseStep) * kRebaseStep;
    double nz = std::floor(focusZ / kRebaseStep) * kRebaseStep;
    if (nx == a.originX && nz == a.originZ) return;
    float dx = static_cast<float>(nx - a.originX), dz = static_cast<float>(nz - a.originZ);
    a.originX = nx;
    a.originZ = nz;
    a.cam.position.x -= dx;
    a.cam.position.z -= dz;
    a.chase.shiftOrigin(dx, dz);
}

// Put the boat in open water just off an island, pointing along the coast.
void spawnBoat(App& a, double nearX, double nearZ, IslandType kind = IslandType::Mountain)
{
    IslandInfo is = a.world->nearestIsland(nearX, nearZ, kind);
    if (is.type == IslandType::None) is = a.world->nearestIsland(nearX, nearZ);
    double cx = is.type != IslandType::None ? is.cx : nearX;
    double cz = is.type != IslandType::None ? is.cz : nearZ;

    const double dirX = -0.8, dirZ = 0.6; // approach from the south-west
    double r = is.radius * 0.5;
    // Walk outward from the centre until the water is comfortably deep.
    while (r < is.radius * 3.0 + 400.0 && a.world->height(cx + dirX * r, cz + dirZ * r) > -9.0) r += 10.0;
    r += 60.0;
    double bx = cx + dirX * r, bz = cz + dirZ * r;

    // Heading: roughly along the coast, with the island off the starboard bow.
    float toIsland = std::atan2(static_cast<float>(cz - bz), static_cast<float>(cx - bx));
    a.boat.placeAt(bx, bz, wrapAngle(toIsland - 1.1f));

    a.originX = std::floor(bx / kRebaseStep) * kRebaseStep;
    a.originZ = std::floor(bz / kRebaseStep) * kRebaseStep;
    a.chase.reset();
    a.chase.snapBehind(a.boat.heading());
    a.chase.pitch = 0.45f;
    a.chase.distance = 16.0f;
}

void rebuildWorld(App& a, uint32_t seed)
{
    a.terrain.reset();  // joins the worker threads before the world they read goes away
    a.world = std::make_unique<IslandGenerator>(seed);
    a.terrain = std::make_unique<TerrainStreamer>(*a.world, a.opt.radius);
    a.landmarks = Landmarks();
}

// Camera presets for screenshots.
void applyView(App& a, const std::string& view)
{
    float h = a.boat.heading();
    if (view == "side") {
        a.chase.yaw = h + kPi * 0.5f; a.chase.pitch = 0.22f; a.chase.distance = 14.0f;
    } else if (view == "high") {
        a.chase.yaw = h + kPi * 0.8f; a.chase.pitch = 0.95f; a.chase.distance = 60.0f;
    } else if (view == "low") {
        a.chase.yaw = h + kPi * 1.15f; a.chase.pitch = 0.18f; a.chase.distance = 18.0f;
    } else if (view == "cove") {
        a.chase.yaw = h - kPi * 0.6f; a.chase.pitch = 0.45f; a.chase.distance = 26.0f;
    } else { // chase
        a.chase.yaw = h + kPi; a.chase.pitch = 0.45f; a.chase.distance = 16.0f;
    }
}

// ------------------------------------------------------------------ input ---

void setMouseCapture(App& a, bool on)
{
    a.mouseCaptured = on;
    a.firstMouse = true;
    glfwSetInputMode(a.window, GLFW_CURSOR, on ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    if (on && glfwRawMouseMotionSupported()) glfwSetInputMode(a.window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
}

void onScroll(GLFWwindow*, double, double dy) { g_app->scrollAccum += static_cast<float>(dy); }

void onKey(GLFWwindow* w, int key, int, int action, int)
{
    if (action != GLFW_PRESS) return;
    App& a = *g_app;
    switch (key) {
    case GLFW_KEY_ESCAPE:    glfwSetWindowShouldClose(w, GLFW_TRUE); break;
    case GLFW_KEY_TAB:       setMouseCapture(a, !a.mouseCaptured); break;
    case GLFW_KEY_H:         a.showHud = !a.showHud; break;
    case GLFW_KEY_F:         a.wireframe = !a.wireframe; break;
    case GLFW_KEY_T:         a.env.paused = !a.env.paused; break;
    case GLFW_KEY_L:         a.boatLights = !a.boatLights; break;
    case GLFW_KEY_V:
        a.freeCam = !a.freeCam;
        if (!a.freeCam) a.chase.reset();
        break;
    case GLFW_KEY_BACKSPACE: spawnBoat(a, a.boat.x(), a.boat.z()); break;
    case GLFW_KEY_N: {
        uint32_t seed = static_cast<uint32_t>(std::rand()) * 2654435761u + static_cast<uint32_t>(std::time(nullptr));
        rebuildWorld(a, seed);
        spawnBoat(a, a.boat.x(), a.boat.z());
        std::printf("New world: seed %u\n", seed);
        break;
    }
    case GLFW_KEY_R:
        for (Shader* s : a.allShaders()) s->reload();
        std::printf("Shaders reloaded\n");
        break;
    case GLFW_KEY_P: {
        int fw, fh;
        glfwGetFramebufferSize(w, &fw, &fh);
        char name[64];
        std::snprintf(name, sizeof(name), "seasail_%02d.bmp", a.screenshotCount++);
        if (saveBmp(name, fw, fh)) std::printf("Saved %s\n", name);
        break;
    }
    default: break;
    }
}

// ------------------------------------------------------------------ update ---

// Lowest the camera may go at local position p: above the waves and ground.
float cameraFloor(const App& a, const Vec3& p)
{
    double wx = a.originX + p.x, wz = a.originZ + p.z;
    float water = a.ocean->sample(wx, wz, a.time).height + 0.8f;
    float ground = a.world->height(wx, wz) + 1.2f;
    return std::max(water, ground);
}

void stepSimulation(App& a, const BoatInput& in, float mouseDx, float mouseDy, float scroll, float dt)
{
    a.time += dt;
    a.boat.update(in, *a.ocean, *a.world, a.time, dt);

    if (!a.freeCam) {
        rebaseOrigin(a, a.boat.x(), a.boat.z());
        a.chase.update(mouseDx, mouseDy, scroll, boatLocal(a), dt);
        a.chase.apply(a.cam, cameraFloor(a, a.chase.eye()));
    }
}

void update(App& a, float dt)
{
    GLFWwindow* w = a.window;
    auto key = [&](int k) { return glfwGetKey(w, k) == GLFW_PRESS ? 1.0f : 0.0f; };

    float mdx = 0.0f, mdy = 0.0f;
    if (a.mouseCaptured) {
        double mx, my;
        glfwGetCursorPos(w, &mx, &my);
        if (!a.firstMouse) {
            mdx = static_cast<float>(mx - a.lastMouseX);
            mdy = static_cast<float>(my - a.lastMouseY);
        }
        a.lastMouseX = mx;
        a.lastMouseY = my;
        a.firstMouse = false;
    }
    float scroll = a.scrollAccum;
    a.scrollAccum = 0.0f;

    BoatInput in;
    if (!a.freeCam) {
        in.throttle = key(GLFW_KEY_W) - key(GLFW_KEY_S);
        in.steer    = key(GLFW_KEY_D) - key(GLFW_KEY_A);
        in.boost    = key(GLFW_KEY_LEFT_SHIFT) > 0.0f || key(GLFW_KEY_RIGHT_SHIFT) > 0.0f;
    }
    stepSimulation(a, in, mdx, mdy, scroll, dt);

    if (a.freeCam) {
        // Free-flying camera for exploring; the boat idles.
        FlightInput fi;
        fi.move  = {key(GLFW_KEY_D) - key(GLFW_KEY_A), key(GLFW_KEY_SPACE) - key(GLFW_KEY_C), key(GLFW_KEY_W) - key(GLFW_KEY_S)};
        fi.yaw   = mdx * 0.0025f;
        fi.pitch = mdy * 0.0025f;
        float alt = a.cam.position.y - std::max(0.0f, a.world->height(camWorldX(a), camWorldZ(a)));
        float speed = clampf(alt * 0.8f, 6.0f, 1500.0f) * (key(GLFW_KEY_LEFT_SHIFT) > 0.0f ? 6.0f : 1.0f);
        a.cam.fly(fi, speed, dt);
        a.cam.levelTowards({0, 1, 0}, std::min(1.0f, dt * 4.0f));
        a.cam.position.y = std::max(a.cam.position.y, cameraFloor(a, a.cam.position));
        rebaseOrigin(a, camWorldX(a), camWorldZ(a));
    }

    // [ and ] scrub through the day at 3 hours per second.
    a.env.update(dt, (key(GLFW_KEY_RIGHT_BRACKET) - key(GLFW_KEY_LEFT_BRACKET)) * 3.0f);
}

// Collect the lights that matter this frame: the boat's lamps and the
// nearest lighthouse / jetty lanterns.
void gatherLights(App& a)
{
    Lights& L = a.lights;
    L = Lights{};
    // Lamps come on as dusk falls.
    float lampsOn = smoothstepf(0.25f, 0.7f, a.env.night());
    if (lampsOn <= 0.0f) return;

    if (a.boatLights) {
        Mat4 bt = a.boat.transform(a.originX, a.originZ);
        L.add(bt.transformPoint(kMastLampPos), 26.0f, Vec3{1.0f, 0.78f, 0.48f} * (1.2f * lampsOn));
        L.add(bt.transformPoint(kDeckLampPos), 9.0f, Vec3{1.0f, 0.70f, 0.40f} * (0.45f * lampsOn));
        L.spotPos = bt.transformPoint(kSpotlightPos);
        L.spotDir = normalize(a.boat.orientation().rotate(kSpotlightDir));
        L.spotColor = Vec3{1.0f, 0.95f, 0.85f} * (9.0f * lampsOn);
    }

    // Nearest landmark lights first.
    struct Cand { float d2; Vec3 p; float range; Vec3 c; };
    std::vector<Cand> cands;
    for (const Landmark& lm : a.landmarks.all()) {
        Vec3 base = toLocal(a, lm.x, lm.y, lm.z);
        Quat q = Quat::fromAxisAngle({0, 1, 0}, -lm.yaw);
        Vec3 p = base;
        float range = 0.0f;
        Vec3 c;
        switch (lm.kind) {
        case LandmarkKind::Lighthouse: p = base + Vec3{0, kLighthouseLampY, 0}; range = 90.0f; c = Vec3{1.0f, 0.86f, 0.6f} * 6.0f; break;
        case LandmarkKind::Hut:        p = base + q.rotate({1.85f, kHutLanternY, 0.75f}); range = 14.0f; c = Vec3{1.0f, 0.7f, 0.35f} * 1.6f; break;
        case LandmarkKind::Jetty:      p = base + q.rotate({kJettyLength - 0.5f, 2.4f, -1.1f}); range = 16.0f; c = Vec3{1.0f, 0.7f, 0.35f} * 1.6f; break;
        }
        Vec3 d = p - a.cam.position;
        cands.push_back({dot(d, d), p, range, c * lampsOn});
    }
    std::sort(cands.begin(), cands.end(), [](const Cand& x, const Cand& y) { return x.d2 < y.d2; });
    for (const Cand& c : cands) L.add(c.p, c.range, c.c);
}

// ------------------------------------------------------------------ render ---

void setCommonUniforms(const Shader& s, const App& a)
{
    const Environment& e = a.env;
    s.set("uCamPos", a.cam.position);
    s.set("uSunDir", e.sunDir());
    s.set("uSunColor", e.sunColor());
    s.set("uSunIntensity", e.sunIntensity());
    s.set("uMoonDir", e.moonDir());
    s.set("uMoonLight", e.moonLight());
    s.set("uNight", e.night());
    s.set("uFogDensity", 0.00024f);
    s.set("uViewDistance", a.terrain->viewDistance());
    s.set("uExposure", e.exposure());
    s.setVec2("uOriginMod", static_cast<float>(std::fmod(a.originX, 4096.0)), static_cast<float>(std::fmod(a.originZ, 4096.0)));
    s.setVec2("uCloudOffset", static_cast<float>(e.cloudX), static_cast<float>(e.cloudZ));
    s.set("uTime", static_cast<float>(std::fmod(a.time, 3600.0)));
    s.setVec4Array("uPointLights", a.lights.pos, 6);
    s.setVec4Array("uPointColors", a.lights.col, 6);
    s.set("uPointCount", a.lights.count);
    s.set("uSpotPos", a.lights.spotPos);
    s.set("uSpotDir", a.lights.spotDir);
    s.set("uSpotColor", a.lights.spotColor);
}

void drawHud(App& a, int w, int h)
{
    Hud& hud = *a.hud;
    hud.begin();
    const float s = std::max(1.0f, static_cast<float>(h) / 720.0f);  // scale with resolution
    const float cx = 92.0f * s, cy = 92.0f * s, r = 60.0f * s;
    const Rgba white{1, 1, 1, 0.85f}, faint{1, 1, 1, 0.35f}, shade{0.02f, 0.05f, 0.07f, 0.45f};

    // Compass: north (-z) is up, east (+x) is right.
    auto toScreen = [](float wx, float wz) { return Vec3{wx, -wz, 0.0f}; };
    hud.disc(cx, cy, r + 6.0f * s, shade);
    hud.ring(cx, cy, r, 2.0f * s, white);
    for (int i = 0; i < 8; ++i) {
        float ang = i * kPi / 4.0f;
        float len = (i == 0 ? 14.0f : (i % 2 == 0 ? 9.0f : 5.0f)) * s;
        Rgba col = i == 0 ? Rgba{1.0f, 0.45f, 0.3f, 0.95f} : white;
        float dx = std::sin(ang), dy = std::cos(ang);
        hud.line(cx + dx * r, cy + dy * r, cx + dx * (r - len), cy + dy * (r - len), 2.0f * s, col);
    }
    // Where the camera is looking.
    Vec3 cf = a.cam.forward();
    Vec3 cv = toScreen(cf.x, cf.z);
    float cl = std::sqrt(cv.x * cv.x + cv.y * cv.y);
    if (cl > 1e-3f) hud.line(cx, cy, cx + cv.x / cl * r * 0.85f, cy + cv.y / cl * r * 0.85f, 1.5f * s, faint);
    // Boat heading arrow.
    Vec3 bf = a.boat.forward();
    Vec3 d = toScreen(bf.x, bf.z);
    Vec3 n{-d.y, d.x, 0.0f};
    float tipR = r * 0.78f, baseR = r * 0.30f, half = 9.0f * s;
    hud.triangle(cx + d.x * tipR, cy + d.y * tipR,
                 cx - d.x * baseR + n.x * half, cy - d.y * baseR + n.y * half,
                 cx - d.x * baseR - n.x * half, cy - d.y * baseR - n.y * half, white);

    // Clock: a small dial above the compass; the dot is the sun by day, the moon by night.
    const float kx = cx, ky = cy + r + 30.0f * s, kr = 14.0f * s;
    hud.disc(kx, ky, kr + 3.0f * s, shade);
    hud.ring(kx, ky, kr, 1.5f * s, faint);
    float ang = (a.env.hours / 24.0f) * 2.0f * kPi;           // midnight at the bottom
    float night = a.env.night();
    Rgba dot = night > 0.5f ? Rgba{0.75f, 0.82f, 1.0f, 0.95f} : Rgba{1.0f, 0.8f, 0.3f, 0.95f};
    hud.disc(kx - std::sin(ang) * kr, ky - std::cos(ang) * kr, 4.0f * s, dot);
    if (a.env.paused) hud.rect(kx - 4.0f * s, ky - 4.0f * s, 8.0f * s, 8.0f * s, faint);

    // Speed gauge (fills with speed; amber while boosting).
    const float gx = cx + r + 18.0f * s, gy = cy - r, gw = 9.0f * s, gh = 2.0f * r;
    hud.rect(gx - 2 * s, gy - 2 * s, gw + 4 * s, gh + 4 * s, shade);
    float sp = clampf(std::fabs(a.boat.speed()) / 12.0f, 0.0f, 1.0f);
    bool boosting = glfwGetKey(a.window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS && a.boat.engine() > 0.1f;
    hud.rect(gx, gy, gw, gh * sp, boosting ? Rgba{1.0f, 0.7f, 0.25f, 0.9f} : Rgba{0.55f, 0.9f, 1.0f, 0.85f});

    // Engine telegraph: centre line = stop, up = ahead, down = astern.
    const float ex = gx + gw + 10.0f * s;
    hud.rect(ex - 2 * s, gy - 2 * s, gw + 4 * s, gh + 4 * s, shade);
    hud.rect(ex - 3 * s, cy - 1 * s, gw + 6 * s, 2 * s, faint);
    float e = clampf(a.boat.engine(), -1.0f, 1.0f);
    if (e >= 0.0f) hud.rect(ex, cy, gw, r * e, white);
    else           hud.rect(ex, cy + r * e, gw, -r * e, Rgba{1.0f, 0.5f, 0.4f, 0.85f});

    hud.flush(*a.hudShader, w, h);
}

void drawLandmarks(App& a, const Frustum& frustum)
{
    for (const Landmark& lm : a.landmarks.all()) {
        Vec3 p = toLocal(a, lm.x, lm.y, lm.z);
        Vec3 d = p - a.cam.position;
        if (dot(d, d) > 3500.0f * 3500.0f) continue;
        const float r = lm.kind == LandmarkKind::Jetty ? 35.0f : 25.0f;  // rough bounding box
        if (!frustum.intersectsBox(p - Vec3{r, r, r}, p + Vec3{r, r, r})) continue;
        const Mesh* mesh = lm.kind == LandmarkKind::Lighthouse ? a.lighthouseMesh.get()
                         : lm.kind == LandmarkKind::Jetty ? a.jettyMesh.get() : a.hutMesh.get();
        a.objectShader->set("uModel", makeTransform(p, Quat::fromAxisAngle({0, 1, 0}, -lm.yaw)));
        mesh->draw();
    }
}

void drawBeams(App& a, const Mat4& viewProj, int fbW, int fbH)
{
    float intensity = smoothstepf(0.3f, 0.8f, a.env.night());
    if (intensity <= 0.0f) return;
    a.beamShader->use();
    a.beamShader->set("uViewProj", viewProj);
    a.beamShader->set("uCamPos", a.cam.position);
    a.beamShader->set("uSceneDistance", 1);
    a.beamShader->setVec2("uScreenSize", static_cast<float>(fbW), static_cast<float>(fbH));
    a.beamShader->set("uIntensity", intensity);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);   // light adds up
    glDepthMask(GL_FALSE);
    for (const Landmark& lm : a.landmarks.all()) {
        if (lm.kind != LandmarkKind::Lighthouse) continue;
        Vec3 lamp = toLocal(a, lm.x, lm.y + kLighthouseLampY + 1.0f, lm.z);
        float spin = static_cast<float>(std::fmod(a.time * 0.7 + lm.phase, 6.283185307));
        for (float extra : {0.0f, kPi}) {  // two beams, back to back
            a.beamShader->set("uModel", makeTransform(lamp, Quat::fromAxisAngle({0, 1, 0}, spin + extra)));
            a.beamMesh->draw();
        }
    }
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

void render(App& a, int fbW, int fbH)
{
    if (!a.scene->resize(fbW, fbH)) return;
    const float aspect = static_cast<float>(fbW) / static_cast<float>(fbH);
    Mat4 viewProj = Mat4::perspective(a.cam.fovY, aspect, 0.15f, 30000.0f) * a.cam.viewMatrix();
    Frustum frustum(viewProj);
    const GLenum fill = a.wireframe ? GL_LINE : GL_FILL;
    gatherLights(a);

    // ---- pass 1: sky, terrain, plants, landmarks and the boat (HDR) -------------
    a.scene->bind();
    glClear(GL_DEPTH_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glBindVertexArray(a.emptyVao);
    a.skyShader->use();
    setCommonUniforms(*a.skyShader, a);
    a.skyShader->set("uCamRight", a.cam.right());
    a.skyShader->set("uCamUp", a.cam.up());
    a.skyShader->set("uCamForward", a.cam.forward());
    a.skyShader->set("uTanHalfFov", std::tan(a.cam.fovY * 0.5f));
    a.skyShader->set("uAspect", aspect);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glPolygonMode(GL_FRONT_AND_BACK, fill);
    a.terrainShader->use();
    setCommonUniforms(*a.terrainShader, a);
    a.terrainShader->set("uViewProj", viewProj);
    a.terrainShader->set("uWireframe", a.wireframe ? 1 : 0);
    a.terrain->draw(*a.terrainShader, frustum, a.originX, a.originZ);

    // Trees, rocks and gulls: one instanced draw per kind.
    a.terrain->gatherProps(frustum, a.originX, a.originZ, a.cam.position, kPropFadeEnd, a.visibleProps);
    a.landmarks.gulls(*a.world, camWorldX(a), camWorldZ(a), a.originX, a.originZ, a.time,
                      a.visibleProps[static_cast<int>(PropType::Gull)]);
    a.propShader->use();
    setCommonUniforms(*a.propShader, a);
    a.propShader->set("uViewProj", viewProj);
    a.propShader->set("uPropFadeStart", kPropFadeStart);
    a.propShader->set("uPropFadeEnd", kPropFadeEnd);
    a.props->draw(*a.propShader, a.visibleProps);

    a.objectShader->use();
    setCommonUniforms(*a.objectShader, a);
    a.objectShader->set("uViewProj", viewProj);
    drawLandmarks(a, frustum);
    a.objectShader->set("uModel", a.boat.transform(a.originX, a.originZ));
    a.boatMesh->draw();
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    // ---- pass 2: tone-mapped, anti-aliased scene to the screen ------------------
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, fbW, fbH);
    glClear(GL_DEPTH_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    glBindVertexArray(a.emptyVao);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, a.scene->colorTex());
    a.compositeShader->use();
    setCommonUniforms(*a.compositeShader, a);
    a.compositeShader->set("uScene", 0);
    a.compositeShader->setVec2("uScreenSize", static_cast<float>(fbW), static_cast<float>(fbH));
    glDrawArrays(GL_TRIANGLES, 0, 3);

    // ---- pass 3: the sea, looking through itself at the scene -------------------
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, a.scene->distanceTex());
    a.waterShader->use();
    setCommonUniforms(*a.waterShader, a);
    a.ocean->setUniforms(*a.waterShader, a.originX, a.originZ, a.time);
    a.waterShader->set("uViewProj", viewProj);
    a.waterShader->setVec2("uCamXZ", a.cam.position.x, a.cam.position.z);
    a.waterShader->setVec2("uScreenSize", static_cast<float>(fbW), static_cast<float>(fbH));
    a.waterShader->set("uScene", 0);
    a.waterShader->set("uSceneDistance", 1);
    a.waterShader->set("uWireframe", a.wireframe ? 1 : 0);

    // Wake points, newest first, relative to the floating origin.
    float wake[64 * 4] = {};
    int nWake = 0;
    for (const WakePoint& p : a.boat.wake()) {
        if (nWake == 64) break;
        wake[nWake * 4 + 0] = static_cast<float>(p.x - a.originX);
        wake[nWake * 4 + 1] = static_cast<float>(p.z - a.originZ);
        wake[nWake * 4 + 2] = static_cast<float>(a.time - p.time);
        wake[nWake * 4 + 3] = p.strength;
        ++nWake;
    }
    a.waterShader->setVec4Array("uWake", wake, std::max(nWake, 1));
    a.waterShader->set("uWakeCount", nWake);
    Vec3 bl = boatLocal(a);
    a.waterShader->setVec2("uBoatXZ", bl.x, bl.z);

    glPolygonMode(GL_FRONT_AND_BACK, fill);
    a.ocean->draw();
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    // ---- pass 4: lighthouse beams over everything ---------------------------------
    drawBeams(a, viewProj, fbW, fbH);
    glActiveTexture(GL_TEXTURE0);

    if (a.showHud) drawHud(a, fbW, fbH);
}

// ----------------------------------------------------------------- shutdown --

void shutdown(App& a)
{
    // GL objects must be freed while the context still exists.
    a.terrain.reset();
    a.world.reset();
    a.ocean.reset();
    a.scene.reset();
    a.boatMesh.reset();
    a.lighthouseMesh.reset();
    a.jettyMesh.reset();
    a.hutMesh.reset();
    a.beamMesh.reset();
    a.props.reset();
    a.hud.reset();
    a.skyShader.reset();
    a.terrainShader.reset();
    a.objectShader.reset();
    a.propShader.reset();
    a.waterShader.reset();
    a.compositeShader.reset();
    a.beamShader.reset();
    a.hudShader.reset();
    if (a.emptyVao) glDeleteVertexArrays(1, &a.emptyVao);
    glfwTerminate();
}

void printControls()
{
    std::printf(
        "\nControls\n"
        "  W / S      throttle ahead / astern     A / D      steer\n"
        "  Shift      boost                       Mouse      orbit camera   Wheel  zoom\n"
        "  [ / ]      time of day back / forward  T          pause / resume the clock\n"
        "  L          boat lights on / off        V          free camera\n"
        "  Backspace  respawn by an island        N          new world\n"
        "  Tab        release/capture mouse       H          hide HUD       F  wireframe\n"
        "  R          reload shaders              P          screenshot     Esc  quit\n\n");
}

float bearingDegrees(const Vec3& f)
{
    float deg = std::atan2(f.x, -f.z) * 180.0f / kPi;  // 0 = north (-z), 90 = east (+x)
    return deg < 0.0f ? deg + 360.0f : deg;
}

std::unique_ptr<Mesh> buildMesh(void (*fn)(MeshBuilder&))
{
    MeshBuilder mb;
    fn(mb);
    auto m = std::make_unique<Mesh>();
    m->upload(mb);
    return m;
}

} // namespace

// --------------------------------------------------------------------- main --

int main(int argc, char** argv)
{
    App app;
    g_app = &app;
    app.opt = parseArgs(argc, argv);
    app.time = app.opt.time;
    app.env.hours = app.opt.hour;
    std::srand(static_cast<unsigned>(std::time(nullptr)));

    if (!glfwInit()) {
        std::fprintf(stderr, "Failed to initialise GLFW\n");
        return 1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    glfwWindowHint(GLFW_SAMPLES, 4);
    const bool shotMode = !app.opt.shot.empty();
    if (shotMode) glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

    app.window = glfwCreateWindow(app.opt.width, app.opt.height, "SeaSail", nullptr, nullptr);
    if (!app.window) {
        std::fprintf(stderr, "Failed to create an OpenGL 3.3 window\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(app.window);
    glfwSwapInterval(1);
    glfwSetKeyCallback(app.window, onKey);
    glfwSetScrollCallback(app.window, onScroll);
    if (!gladLoadGL(glfwGetProcAddress)) {
        std::fprintf(stderr, "Failed to load OpenGL functions\n");
        glfwTerminate();
        return 1;
    }
    std::printf("OpenGL %s | %s\n", glGetString(GL_VERSION), glGetString(GL_RENDERER));
    glEnable(GL_MULTISAMPLE);

    auto load = [](const char* vs, const char* fs) {
        return std::make_unique<Shader>(findShaderFile(vs), findShaderFile(fs));
    };
    app.skyShader       = load("fullscreen.vert", "sky.frag");
    app.terrainShader   = load("terrain.vert", "terrain.frag");
    app.objectShader    = load("object.vert", "object.frag");
    app.propShader      = load("props.vert", "props.frag");
    app.waterShader     = load("water.vert", "water.frag");
    app.compositeShader = load("fullscreen.vert", "composite.frag");
    app.beamShader      = load("beam.vert", "beam.frag");
    app.hudShader       = load("hud.vert", "hud.frag");
    for (Shader* s : app.allShaders()) {
        if (!s->valid()) {
            std::fprintf(stderr, "Shader setup failed (is the shaders/ folder next to the exe?)\n");
            shutdown(app);
            return 1;
        }
    }
    glGenVertexArrays(1, &app.emptyVao);

    app.ocean = std::make_unique<Ocean>();
    app.scene = std::make_unique<SceneTarget>();
    app.hud   = std::make_unique<Hud>();
    app.props = std::make_unique<PropRenderer>();
    app.boatMesh       = buildMesh([](MeshBuilder& m) { buildBoatModel(m); });
    app.lighthouseMesh = buildMesh(buildLighthouse);
    app.jettyMesh      = buildMesh(buildJetty);
    app.hutMesh        = buildMesh(buildHut);
    app.beamMesh       = std::make_unique<BeamMesh>();
    rebuildWorld(app, app.opt.seed);
    spawnBoat(app, 0.0, 0.0, app.opt.island);
    std::printf("Seed %u | terrain view %.1f km | ocean mesh %zu vertices\n",
                app.opt.seed, app.terrain->viewDistance() / 1000.0f, app.ocean->vertexCount());

    // ---- screenshot / test mode -----------------------------------------------
    if (shotMode) {
        const float dt = 1.0f / 60.0f;
        double startX = app.boat.x(), startZ = app.boat.z();
        float maxSpeed = 0.0f;
        int shoreHits = 0;
        BoatInput in;
        in.throttle = app.opt.simulate > 0.0 ? 1.0f : 0.0f;
        in.steer = app.opt.steer;
        app.env.paused = true;
        int steps = static_cast<int>(app.opt.simulate / dt);
        for (int i = 0; i < steps; ++i) {
            stepSimulation(app, in, 0.0f, 0.0f, 0.0f, dt);
            maxSpeed = std::max(maxSpeed, app.boat.speed());
            if (app.boat.hitShoreThisFrame()) ++shoreHits;
        }
        if (steps > 0)
            std::printf("Simulated %.1f s: travelled %.0f m, top speed %.2f m/s (%.1f kn), final %.2f m/s, "
                        "heading %.0f deg, shore contact %d frames\n",
                        app.opt.simulate, std::hypot(app.boat.x() - startX, app.boat.z() - startZ),
                        maxSpeed, maxSpeed * 1.944f, app.boat.speed(), bearingDegrees(app.boat.forward()), shoreHits);

        applyView(app, app.opt.view);
        for (int i = 0; i < 180; ++i) stepSimulation(app, in, 0, 0, 0, dt); // settle the camera

        auto t0 = std::chrono::steady_clock::now();
        app.terrain->update(camWorldX(app), app.cam.position.y, camWorldZ(app));
        app.landmarks.update(*app.world, camWorldX(app), camWorldZ(app));
        while (!app.terrain->idle()) {
            app.terrain->uploadFinished(1000.0);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        app.terrain->uploadFinished(1000.0);
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();

        int fw, fh;
        glfwGetFramebufferSize(app.window, &fw, &fh);
        render(app, fw, fh);
        glFinish();
        const ChunkStats& st = app.terrain->stats();
        std::printf("Streamed %d chunks in %.0f ms | drew %d chunks, %.2fM triangles, %zu props, %zu landmarks\n",
                    st.loaded, ms, st.drawn, st.triangles / 1e6, st.props, app.landmarks.all().size());
        bool ok = saveBmp(app.opt.shot, fw, fh);
        std::printf(ok ? "Saved %s\n" : "Could not write %s\n", app.opt.shot.c_str());
        shutdown(app);
        return ok ? 0 : 1;
    }

    // ---- interactive loop ----------------------------------------------------------
    printControls();
    setMouseCapture(app, true);
    double last = glfwGetTime(), titleTimer = last;
    int frames = 0;
    while (!glfwWindowShouldClose(app.window)) {
        double now = glfwGetTime();
        float dt = static_cast<float>(std::min(now - last, 0.05));
        last = now;

        glfwPollEvents();
        update(app, dt);
        app.terrain->update(camWorldX(app), app.cam.position.y, camWorldZ(app));
        app.terrain->uploadFinished(3.0);  // ms per frame for GPU uploads
        app.landmarks.update(*app.world, camWorldX(app), camWorldZ(app));

        int fw, fh;
        glfwGetFramebufferSize(app.window, &fw, &fh);
        if (fw > 0 && fh > 0) render(app, fw, fh);
        glfwSwapBuffers(app.window);

        ++frames;
        if (now - titleTimer >= 0.5) {
            const ChunkStats& st = app.terrain->stats();
            int hh = static_cast<int>(app.env.hours), mm = static_cast<int>((app.env.hours - hh) * 60.0f);
            char title[256];
            std::snprintf(title, sizeof(title),
                          "SeaSail | %.0f fps | %02d:%02d%s | %.1f kn | heading %03.0f | %.2f, %.2f km | chunks %d/%d | props %zu%s%s",
                          frames / (now - titleTimer), hh, mm, app.env.paused ? " (paused)" : "",
                          std::fabs(app.boat.speed()) * 1.944f, bearingDegrees(app.boat.forward()),
                          app.boat.x() / 1000.0, app.boat.z() / 1000.0, st.drawn, st.loaded, st.props,
                          app.freeCam ? " | FREE CAMERA (V)" : "",
                          app.mouseCaptured ? "" : " | Tab: capture mouse");
            glfwSetWindowTitle(app.window, title);
            frames = 0;
            titleTimer = now;
        }
    }

    shutdown(app);
    return 0;
}
