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
//   M            minimap zoom                  H         hide HUD
//   F            wireframe                     R         reload shaders
//   P            screenshot (.bmp)             Esc       quit
//   ` or F1      developer console (type `help` in it)
//   F3           performance overlay (CPU / GPU time per pass)
//   F11 / Alt+Enter  fullscreen
//
// Command line (handy for testing and README images)
//   SeaSail [--seed N] [--radius CHUNKS] [--width W --height H] [--hour 0..24]
//           [--view chase|side|high|low|cove] [--simulate SECONDS] [--steer -1..1]
//           [--island mountain|atoll|stacks|cay] [--time SECONDS] [--shot out.bmp]
//           [--cmd "console commands"] [--exec file.cfg] [--console]
//           [--fullscreen] [--scale 0.25..1] [--msaa 0|2|4|8] [--bench FRAMES]

#include "Boat.h"
#include "BoatModel.h"
#include "Camera.h"
#include "ChaseCamera.h"
#include "Console.h"
#include "ConsoleView.h"
#include "Environment.h"
#include "Frustum.h"
#include "Hud.h"
#include "IslandGenerator.h"
#include "LandmarkModels.h"
#include "Landmarks.h"
#include "Mesh.h"
#include "Minimap.h"
#include "Profiler.h"
#include "Ocean.h"
#include "PropRenderer.h"
#include "SceneTarget.h"
#include "Screenshot.h"
#include "Shader.h"
#include "TerrainStreamer.h"
#include "TextRenderer.h"

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace sea;

#ifdef _WIN32
// Laptops with two GPUs (NVIDIA Optimus, AMD switchable graphics) run new
// programs on the slower built-in GPU unless the program asks otherwise.
// Exporting these two variables is how a game asks for the fast one.
extern "C" {
__declspec(dllexport) unsigned long NvOptimusEnablement = 1;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

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
    std::vector<std::string> startup;  // console lines to run at start ("exec file" for --exec)
    bool        console  = false;      // start with the console open
    int         bench    = 0;          // with --shot: time this many frames and print a report
    float       scale    = 0.0f;       // render scale, 0 = auto (at most 1080 lines)
    bool        fullscreen = false;
    int         msaa     = 4;          // samples for the window's own framebuffer (HUD edges)
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
        else if (!std::strcmp(argv[i], "--cmd"))      o.startup.push_back(next());
        else if (!std::strcmp(argv[i], "--exec"))     o.startup.push_back(std::string("exec \"") + next() + "\"");
        else if (!std::strcmp(argv[i], "--console"))  o.console  = true;
        else if (!std::strcmp(argv[i], "--bench"))    o.bench    = std::atoi(next());
        else if (!std::strcmp(argv[i], "--scale"))    o.scale    = static_cast<float>(std::atof(next()));
        else if (!std::strcmp(argv[i], "--fullscreen")) o.fullscreen = true;
        else if (!std::strcmp(argv[i], "--msaa"))     o.msaa     = std::atoi(next());
        else if (!std::strcmp(argv[i], "--island")) {
            std::string t = next();
            o.island = t == "atoll" ? IslandType::Atoll : t == "stacks" ? IslandType::SeaStacks
                     : t == "cay" ? IslandType::Cay : IslandType::Mountain;
        }
    }
    o.radius = std::max(8, std::min(o.radius, 64));
    o.msaa   = std::max(0, std::min(o.msaa, 8));
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

constexpr float kDefaultFog = 0.00024f;

struct App {
    Options     opt;
    GLFWwindow* window = nullptr;

    std::unique_ptr<IslandGenerator> world;
    std::unique_ptr<TerrainStreamer> terrain;
    std::unique_ptr<Ocean>           ocean;
    std::unique_ptr<SceneTarget>     scene;
    std::unique_ptr<ScreenTarget>    lowRes;   // 3D view below screen resolution
    std::unique_ptr<Mesh>            boatMesh, lighthouseMesh, jettyMesh, hutMesh;
    std::unique_ptr<BeamMesh>        beamMesh;
    std::unique_ptr<PropRenderer>    props;
    std::unique_ptr<Hud>             hud;
    std::unique_ptr<Minimap>         minimap;
    std::unique_ptr<TextRenderer>    text;
    std::unique_ptr<Profiler>        prof;
    std::unique_ptr<Shader> skyShader, terrainShader, objectShader, propShader, waterShader, compositeShader,
                            beamShader, hudShader, minimapShader, textShader, upscaleShader;
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
    bool        vsync = true;
    float       renderScale = 0.0f;   // 0 = auto
    int         renderW = 0, renderH = 0;  // what the 3D view was last drawn at
    bool        fullscreen = false;
    int         winX = 100, winY = 100, winW = 1280, winH = 720;  // windowed placement to go back to
    float       fogDensity = kDefaultFog;
    Console     console;

    // Floating origin: everything the GPU sees is relative to this point,
    // which hops in 1 km steps to stay near the player.
    double originX = 0.0, originZ = 0.0;
    double time = 0.0;

    bool   mouseCaptured = false, firstMouse = true;
    double lastMouseX = 0.0, lastMouseY = 0.0;
    float  scrollAccum = 0.0f;
    bool   mouseBeforeConsole = false;  // capture state to restore when the console closes
    bool   showHud = true, wireframe = false;
    int    screenshotCount = 0;
    std::string pendingShot;            // save this file after the next frame is drawn
    float  fps = 0.0f;
    bool   showPerf = false;            // F3 overlay
    std::string gpuName;

    std::vector<Shader*> allShaders() const
    {
        return {skyShader.get(), terrainShader.get(), objectShader.get(), propShader.get(), waterShader.get(),
                compositeShader.get(), beamShader.get(), hudShader.get(), minimapShader.get(), textShader.get(),
                upscaleShader.get()};
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
    // Stop everything that reads the old world on other threads first.
    if (a.minimap) a.minimap->reset();
    a.terrain.reset();
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

// Render scale actually used: the setting, or in auto mode whatever keeps
// the 3D view at 1080 lines or fewer (most of the GPU's work is per pixel,
// and a 1440p or 4K screen has 1.8x or 4x the pixels of 1080p).
float effectiveScale(const App& a, int fbH)
{
    if (a.renderScale > 0.0f) return a.renderScale;
    if (fbH <= 1080) return 1.0f;
    return std::floor(1080.0f / static_cast<float>(fbH) * 20.0f) / 20.0f;  // in 5% steps
}

void applyVsync(const App& a) { glfwSwapInterval(a.vsync ? 1 : 0); }

// The monitor the window mostly sits on.
GLFWmonitor* currentMonitor(GLFWwindow* w)
{
    int wx, wy, ww, wh;
    glfwGetWindowPos(w, &wx, &wy);
    glfwGetWindowSize(w, &ww, &wh);
    int count = 0;
    GLFWmonitor** monitors = glfwGetMonitors(&count);
    GLFWmonitor* best = glfwGetPrimaryMonitor();
    int bestArea = 0;
    for (int i = 0; i < count; ++i) {
        int mx, my;
        glfwGetMonitorPos(monitors[i], &mx, &my);
        const GLFWvidmode* m = glfwGetVideoMode(monitors[i]);
        int ox = std::max(0, std::min(wx + ww, mx + m->width) - std::max(wx, mx));
        int oy = std::max(0, std::min(wy + wh, my + m->height) - std::max(wy, my));
        if (ox * oy > bestArea) {
            bestArea = ox * oy;
            best = monitors[i];
        }
    }
    return best;
}

// Fullscreen at the monitor's own resolution and refresh rate. A real
// fullscreen window lets the driver flip frames straight to the screen,
// which paces frames more evenly than a maximised window.
void setFullscreen(App& a, bool on)
{
    if (on == a.fullscreen) return;
    if (on) {
        glfwGetWindowPos(a.window, &a.winX, &a.winY);
        glfwGetWindowSize(a.window, &a.winW, &a.winH);
        GLFWmonitor* mon = currentMonitor(a.window);
        const GLFWvidmode* mode = glfwGetVideoMode(mon);
        glfwSetWindowMonitor(a.window, mon, 0, 0, mode->width, mode->height, mode->refreshRate);
    } else {
        glfwSetWindowMonitor(a.window, nullptr, a.winX, a.winY, a.winW, a.winH, 0);
    }
    a.fullscreen = on;
    applyVsync(a);   // some drivers reset the swap interval on a mode change
}

// Open or close the console. The mouse is freed while it's open and put
// back the way it was afterwards.
void setConsoleOpen(App& a, bool open)
{
    if (open == a.console.isOpen()) return;
    a.console.setOpen(open);
    if (open) {
        a.mouseBeforeConsole = a.mouseCaptured;
        setMouseCapture(a, false);
    } else if (a.mouseBeforeConsole) {
        setMouseCapture(a, true);
    }
}

void newWorld(App& a)
{
    uint32_t seed = static_cast<uint32_t>(std::rand()) * 2654435761u + static_cast<uint32_t>(std::time(nullptr));
    rebuildWorld(a, seed);
    spawnBoat(a, a.boat.x(), a.boat.z());
    a.console.print("New world: seed %u", seed);
}

// The next free "seasail_NN.bmp".
std::string nextShotName(App& a)
{
    char name[64];
    std::snprintf(name, sizeof(name), "seasail_%02d.bmp", a.screenshotCount++);
    return name;
}

void onChar(GLFWwindow*, unsigned int codepoint) { g_app->console.onChar(codepoint); }

// Keys while the console is open: editing only, no game controls.
void consoleKey(App& a, int key, int mods)
{
    const bool ctrl = (mods & GLFW_MOD_CONTROL) != 0;
    Console& c = a.console;
    switch (key) {
    case GLFW_KEY_ESCAPE:    setConsoleOpen(a, false); break;
    case GLFW_KEY_ENTER:
    case GLFW_KEY_KP_ENTER:  c.onKey(ConsoleKey::Enter, ctrl); break;
    case GLFW_KEY_BACKSPACE: c.onKey(ConsoleKey::Backspace, ctrl); break;
    case GLFW_KEY_DELETE:    c.onKey(ConsoleKey::Delete, ctrl); break;
    case GLFW_KEY_LEFT:      c.onKey(ConsoleKey::Left, ctrl); break;
    case GLFW_KEY_RIGHT:     c.onKey(ConsoleKey::Right, ctrl); break;
    case GLFW_KEY_HOME:      c.onKey(ConsoleKey::Home, ctrl); break;
    case GLFW_KEY_END:       c.onKey(ConsoleKey::End, ctrl); break;
    case GLFW_KEY_UP:        c.onKey(ConsoleKey::Up, ctrl); break;
    case GLFW_KEY_DOWN:      c.onKey(ConsoleKey::Down, ctrl); break;
    case GLFW_KEY_TAB:       c.onKey(ConsoleKey::Tab, ctrl); break;
    case GLFW_KEY_PAGE_UP:   c.onKey(ConsoleKey::PageUp, ctrl); break;
    case GLFW_KEY_PAGE_DOWN: c.onKey(ConsoleKey::PageDown, ctrl); break;
    case GLFW_KEY_L:         if (ctrl) c.clear(); break;
    case GLFW_KEY_V:
        if (ctrl) {
            const char* clip = glfwGetClipboardString(a.window);
            if (clip) c.insertText(clip);
        }
        break;
    default: break;
    }
}

void onKey(GLFWwindow* w, int key, int, int action, int mods)
{
    App& a = *g_app;
    if (action == GLFW_PRESS && (key == GLFW_KEY_F11 || (key == GLFW_KEY_ENTER && (mods & GLFW_MOD_ALT)))) {
        setFullscreen(a, !a.fullscreen);   // works with the console open too
        return;
    }
    // ` (the key left of 1) or F1 opens and closes the console.
    if (action == GLFW_PRESS && (key == GLFW_KEY_GRAVE_ACCENT || key == GLFW_KEY_F1)) {
        setConsoleOpen(a, !a.console.isOpen());
        return;
    }
    if (a.console.isOpen()) {
        if (action != GLFW_RELEASE) consoleKey(a, key, mods);  // presses and key repeat
        return;
    }
    if (action != GLFW_PRESS) return;
    switch (key) {
    case GLFW_KEY_ESCAPE:    glfwSetWindowShouldClose(w, GLFW_TRUE); break;
    case GLFW_KEY_TAB:       setMouseCapture(a, !a.mouseCaptured); break;
    case GLFW_KEY_H:         a.showHud = !a.showHud; break;
    case GLFW_KEY_F:         a.wireframe = !a.wireframe; break;
    case GLFW_KEY_T:         a.env.paused = !a.env.paused; break;
    case GLFW_KEY_L:         a.boatLights = !a.boatLights; break;
    case GLFW_KEY_M:         a.minimap->cycleZoom(); break;
    case GLFW_KEY_V:
        a.freeCam = !a.freeCam;
        if (!a.freeCam) a.chase.reset();
        break;
    case GLFW_KEY_BACKSPACE: spawnBoat(a, a.boat.x(), a.boat.z()); break;
    case GLFW_KEY_N:         newWorld(a); break;
    case GLFW_KEY_R:
        for (Shader* s : a.allShaders()) s->reload();
        std::printf("Shaders reloaded\n");
        break;
    case GLFW_KEY_P:         a.pendingShot = nextShotName(a); break;  // saved after the frame is drawn
    case GLFW_KEY_F3:        a.showPerf = !a.showPerf; break;
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
    // While the console is open the keyboard is for typing, not sailing.
    const bool typing = a.console.isOpen();
    auto key = [&](int k) { return !typing && glfwGetKey(w, k) == GLFW_PRESS ? 1.0f : 0.0f; };
    a.console.update(dt);

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
    if (typing) {  // the wheel scrolls the console log instead
        a.console.scrollBy(static_cast<int>(std::lround(scroll * 3.0f)));
        scroll = 0.0f;
    }

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
    s.set("uFogDensity", a.fogDensity);
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
    const float s = std::max(1.0f, static_cast<float>(h) / 720.0f);  // scale with resolution
    const float r = 80.0f * s, cx = r + 22.0f * s, cy = r + 22.0f * s;
    const Rgba white{1, 1, 1, 0.85f}, faint{1, 1, 1, 0.35f}, shade{0.02f, 0.05f, 0.07f, 0.45f};

    // Minimap: the painted chart first, then everything drawn on top of it.
    a.minimap->draw(*a.minimapShader, cx, cy, r, w, h, a.boat.x(), a.boat.z(), a.env.night());

    Hud& hud = *a.hud;
    hud.begin();
    const float range = a.minimap->range();
    // North (-z) is up, east (+x) is right. World offset -> pixels from centre.
    auto toMap = [&](double dx, double dz) { return Vec3{static_cast<float>(dx) / range * r, -static_cast<float>(dz) / range * r, 0.0f}; };

    // Lighthouses (red and white) and jetties (brown) within range.
    for (const Landmark& lm : a.landmarks.all()) {
        if (lm.kind == LandmarkKind::Hut) continue;
        Vec3 p = toMap(lm.x - a.boat.x(), lm.z - a.boat.z());
        if (p.x * p.x + p.y * p.y > (r - 6.0f * s) * (r - 6.0f * s)) continue;
        if (lm.kind == LandmarkKind::Lighthouse) {
            hud.disc(cx + p.x, cy + p.y, 4.5f * s, Rgba{1.0f, 1.0f, 1.0f, 0.95f}, 12);
            hud.disc(cx + p.x, cy + p.y, 2.5f * s, Rgba{0.85f, 0.15f, 0.1f, 1.0f}, 12);
        } else {
            Vec3 d = toMap(std::cos(lm.yaw), std::sin(lm.yaw));   // jetty points out to sea
            float l = std::sqrt(d.x * d.x + d.y * d.y);
            if (l > 1e-4f) { d.x /= l; d.y /= l; }
            hud.line(cx + p.x, cy + p.y, cx + p.x + d.x * 6.0f * s, cy + p.y + d.y * 6.0f * s, 3.0f * s, Rgba{0.45f, 0.30f, 0.15f, 1.0f});
        }
    }

    // Rim: compass ticks with north in red.
    hud.ring(cx, cy, r, 2.0f * s, white);
    for (int i = 0; i < 8; ++i) {
        float ang = i * kPi / 4.0f;
        float len = (i == 0 ? 12.0f : (i % 2 == 0 ? 8.0f : 4.0f)) * s;
        Rgba col = i == 0 ? Rgba{1.0f, 0.45f, 0.3f, 0.95f} : white;
        float dx = std::sin(ang), dy = std::cos(ang);
        hud.line(cx + dx * r, cy + dy * r, cx + dx * (r - len), cy + dy * (r - len), 2.0f * s, col);
    }
    // Where the camera is looking: a faint line from the boat.
    Vec3 cf = a.cam.forward();
    Vec3 cv = toMap(cf.x, cf.z);
    float cl = std::sqrt(cv.x * cv.x + cv.y * cv.y);
    if (cl > 1e-3f) hud.line(cx, cy, cx + cv.x / cl * r * 0.35f, cy + cv.y / cl * r * 0.35f, 1.5f * s, faint);
    // The boat: a small arrow at the centre, pointing where it's heading.
    Vec3 bf = a.boat.forward();
    Vec3 d = toMap(bf.x, bf.z);
    float dl = std::sqrt(d.x * d.x + d.y * d.y);
    d.x /= dl; d.y /= dl;
    Vec3 n{-d.y, d.x, 0.0f};
    const float tip = 9.0f * s, back = 5.0f * s, half = 5.5f * s;
    hud.triangle(cx + d.x * tip, cy + d.y * tip,
                 cx - d.x * back + n.x * half, cy - d.y * back + n.y * half,
                 cx - d.x * back - n.x * half, cy - d.y * back - n.y * half, Rgba{1.0f, 0.85f, 0.3f, 1.0f});
    // Zoom indicator: one to three dots under the map.
    for (int z = 0; z < 3; ++z) {
        bool on = a.minimap->range() >= Minimap::kRanges[z];
        hud.disc(cx - 8.0f * s + z * 8.0f * s, cy - r - 9.0f * s, 2.5f * s, on ? white : faint, 8);
    }

    // Clock: a small dial above the map; the dot is the sun by day, the moon by night.
    const float kx = cx + r - 8.0f * s, ky = cy + r + 4.0f * s, kr = 14.0f * s;
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
    bool boosting = !a.console.isOpen() && glfwGetKey(a.window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS && a.boat.engine() > 0.1f;
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

// printf into a std::string.
std::string strf(const char* fmt, ...)
{
    char buf[256];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    return buf;
}

// Performance overlay (F3): frame-time graph plus where the time goes on
// the CPU and on the GPU. "avg / max" is over the last 120 frames, so a
// stutter shows up as a max far above the average.
void drawPerf(App& a, int w, int h)
{
    const Profiler& p = *a.prof;
    const int   ts   = std::max(2, h / 540);
    const float lineH = static_cast<float>(10 * ts);
    const float pad  = static_cast<float>(5 * ts);
    const Rgba white{0.95f, 0.97f, 1.0f, 1.0f}, dim{0.65f, 0.72f, 0.78f, 1.0f}, head{0.52f, 0.86f, 1.0f, 1.0f};

    std::vector<std::pair<std::string, Rgba>> lines;
    auto line = [&](Rgba c, std::string t) { lines.emplace_back(std::move(t), c); };
    line(head, strf("%s", a.gpuName.substr(0, 44).c_str()));
    line(dim, strf("%dx%d  3D at %dx%d (%.0f%%%s)", w, h, a.renderW, a.renderH,
                     100.0f * static_cast<float>(a.renderH) / static_cast<float>(std::max(h, 1)),
                     a.renderScale > 0.0f ? "" : " auto"));
    line(dim, strf("vsync %s  %s", a.vsync ? "on" : "off", a.fullscreen ? "fullscreen" : "windowed"));
    float fps = p.frameAvg() > 0.0f ? 1000.0f / p.frameAvg() : 0.0f;
    line(white, strf("%3.0f fps  frame %5.1f ms  worst %5.1f", fps, p.frameAvg(), p.frameMax()));
    line(head, strf("CPU %5.2f ms         avg    max", p.cpuTotal()));
    for (const Profiler::Section& s : p.cpuSections())
        line(dim, strf("  %-16s %6.2f %6.2f", s.name.c_str(), s.avg(), s.max()));
    if (p.gpuSupported()) {
        line(head, strf("GPU %5.2f ms         avg    max", p.gpuTotal()));
        for (const Profiler::Section& s : p.gpuSections())
            line(dim, strf("  %-16s %6.2f %6.2f", s.name.c_str(), s.avg(), s.max()));
    } else {
        line(dim, "GPU timing not supported by this driver");
    }

    // Layout: a panel in the top-right corner, text then the graph.
    size_t cols = 0;
    for (const auto& l : lines) cols = std::max(cols, l.first.size());
    const float graphH = static_cast<float>(40 * ts);
    const float panelW = std::max(TextRenderer::width(cols, ts), static_cast<float>(Profiler::kGraph)) + 2.0f * pad;
    const float panelH = lineH * static_cast<float>(lines.size()) + graphH + 3.0f * pad;
    const float x0 = static_cast<float>(w) - panelW - pad, top = static_cast<float>(h) - pad;

    Hud& hud = *a.hud;
    TextRenderer& text = *a.text;
    hud.begin();
    text.begin();
    hud.rect(x0, top - panelH, panelW, panelH, Rgba{0.02f, 0.05f, 0.07f, 0.82f});
    for (size_t i = 0; i < lines.size(); ++i)
        text.text(x0 + pad, top - pad - lineH * static_cast<float>(i + 1) + static_cast<float>(ts), lines[i].first, ts,
                  lines[i].second);

    // Frame-time graph, oldest on the left. 50 ms fills the height; lines at
    // 16.7 ms (60 fps) and 33.3 ms (30 fps). Green / amber / red bars.
    const float gx = x0 + pad, gy = top - panelH + pad, gw = panelW - 2.0f * pad;
    const float bar = gw / static_cast<float>(Profiler::kGraph);
    hud.rect(gx, gy, gw, graphH, Rgba{1, 1, 1, 0.05f});
    for (int i = 0; i < Profiler::kGraph; ++i) {
        float ms = p.frameTimes()[static_cast<size_t>((p.frameIndex() + i) % Profiler::kGraph)];
        float bh = std::min(ms / 50.0f, 1.0f) * graphH;
        Rgba c = ms < 18.0f ? Rgba{0.45f, 0.85f, 0.45f, 0.9f} : ms < 34.0f ? Rgba{1.0f, 0.75f, 0.3f, 0.9f}
                                                                          : Rgba{1.0f, 0.35f, 0.3f, 0.95f};
        hud.rect(gx + bar * static_cast<float>(i), gy, std::max(bar, 1.0f), bh, c);
    }
    for (float ms : {16.67f, 33.33f}) hud.rect(gx, gy + ms / 50.0f * graphH, gw, 1.0f, Rgba{1, 1, 1, 0.35f});

    hud.flush(*a.hudShader, w, h);
    text.flush(*a.textShader, w, h);
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
    // The 3D view may render below screen resolution (see effectiveScale);
    // the HUD is always drawn at full resolution on top.
    const float scale = effectiveScale(a, fbH);
    const int rw = std::max(1, static_cast<int>(std::lround(static_cast<float>(fbW) * scale)));
    const int rh = std::max(1, static_cast<int>(std::lround(static_cast<float>(fbH) * scale)));
    const bool lowRes = rw != fbW || rh != fbH;
    a.renderW = rw;
    a.renderH = rh;
    if (!a.scene->resize(rw, rh)) return;
    if (lowRes && !a.lowRes->resize(rw, rh)) return;
    const float aspect = static_cast<float>(fbW) / static_cast<float>(fbH);
    Mat4 viewProj = Mat4::perspective(a.cam.fovY, aspect, 0.15f, 30000.0f) * a.cam.viewMatrix();
    Frustum frustum(viewProj);
    const GLenum fill = a.wireframe ? GL_LINE : GL_FILL;
    gatherLights(a);

    Profiler& prof = *a.prof;

    // ---- pass 1: sky, terrain, plants, landmarks and the boat (HDR) -------------
    a.scene->bind();
    glClear(GL_DEPTH_BUFFER_BIT);
    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glPolygonMode(GL_FRONT_AND_BACK, fill);
    prof.gpu("terrain");
    a.terrainShader->use();
    setCommonUniforms(*a.terrainShader, a);
    a.terrainShader->set("uViewProj", viewProj);
    a.terrainShader->set("uWireframe", a.wireframe ? 1 : 0);
    a.terrain->draw(*a.terrainShader, frustum, a.originX, a.originZ, a.cam.position);

    // Trees, rocks and gulls: one instanced draw per kind.
    prof.cpu("cull props");
    a.terrain->gatherProps(frustum, a.originX, a.originZ, a.cam.position, kPropFadeEnd, a.visibleProps);
    a.landmarks.gulls(*a.world, camWorldX(a), camWorldZ(a), a.originX, a.originZ, a.time,
                      a.visibleProps[static_cast<int>(PropType::Gull)]);
    prof.cpu("draw calls");
    prof.gpu("props");
    a.propShader->use();
    setCommonUniforms(*a.propShader, a);
    a.propShader->set("uViewProj", viewProj);
    a.propShader->set("uPropFadeStart", kPropFadeStart);
    a.propShader->set("uPropFadeEnd", kPropFadeEnd);
    a.props->draw(*a.propShader, a.visibleProps);

    prof.gpu("objects");
    a.objectShader->use();
    setCommonUniforms(*a.objectShader, a);
    a.objectShader->set("uViewProj", viewProj);
    drawLandmarks(a, frustum);
    a.objectShader->set("uModel", a.boat.transform(a.originX, a.originZ));
    a.boatMesh->draw();
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    // Sky last: it sits on the far plane, so with LEQUAL it only shades the
    // pixels nothing else covered (no work behind islands and the boat).
    prof.gpu("sky");
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);
    glBindVertexArray(a.emptyVao);
    a.skyShader->use();
    setCommonUniforms(*a.skyShader, a);
    a.skyShader->set("uCamRight", a.cam.right());
    a.skyShader->set("uCamUp", a.cam.up());
    a.skyShader->set("uCamForward", a.cam.forward());
    a.skyShader->set("uTanHalfFov", std::tan(a.cam.fovY * 0.5f));
    a.skyShader->set("uAspect", aspect);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);

    // ---- pass 2: tone-mapped, anti-aliased scene to the screen ------------------
    // (or to the low-res target, stretched over the screen after pass 4)
    prof.gpu("composite");
    if (lowRes) {
        a.lowRes->bind();
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, fbW, fbH);
    }
    glClear(GL_DEPTH_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    glBindVertexArray(a.emptyVao);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, a.scene->colorTex());
    a.compositeShader->use();
    setCommonUniforms(*a.compositeShader, a);
    a.compositeShader->set("uScene", 0);
    a.compositeShader->setVec2("uScreenSize", static_cast<float>(rw), static_cast<float>(rh));
    glDrawArrays(GL_TRIANGLES, 0, 3);

    // ---- pass 3: the sea, looking through itself at the scene -------------------
    prof.gpu("water");
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, a.scene->distanceTex());
    a.waterShader->use();
    setCommonUniforms(*a.waterShader, a);
    a.ocean->setUniforms(*a.waterShader, a.originX, a.originZ, a.time);
    a.waterShader->set("uViewProj", viewProj);
    a.waterShader->setVec2("uCamXZ", a.cam.position.x, a.cam.position.z);
    a.waterShader->setVec2("uScreenSize", static_cast<float>(rw), static_cast<float>(rh));
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
    // Bounding circles for groups of 8 segments, so most pixels can skip
    // most of the wake (see wakeFoam in water.frag).
    float groups[8 * 4] = {};
    for (int g = 0; g * 8 < nWake - 1; ++g) {
        int first = g * 8, last = std::min(first + 8, nWake - 1);  // points first..last
        float cx = 0.0f, cz = 0.0f;
        for (int i = first; i <= last; ++i) { cx += wake[i * 4 + 0]; cz += wake[i * 4 + 1]; }
        cx /= static_cast<float>(last - first + 1);
        cz /= static_cast<float>(last - first + 1);
        float r = 0.0f;
        for (int i = first; i <= last; ++i) {
            float width = 1.0f + wake[i * 4 + 2] * (0.35f + 0.8f * wake[i * 4 + 3]);
            r = std::max(r, std::hypot(wake[i * 4 + 0] - cx, wake[i * 4 + 1] - cz) + width * 1.3f + 0.5f);
        }
        groups[g * 4 + 0] = cx;
        groups[g * 4 + 1] = cz;
        groups[g * 4 + 2] = r;
    }
    a.waterShader->setVec4Array("uWakeGroups", groups, 8);
    Vec3 bl = boatLocal(a);
    a.waterShader->setVec2("uBoatXZ", bl.x, bl.z);

    glPolygonMode(GL_FRONT_AND_BACK, fill);
    a.ocean->draw();
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    // ---- pass 4: lighthouse beams over everything ---------------------------------
    prof.gpu("beams");
    drawBeams(a, viewProj, rw, rh);
    glActiveTexture(GL_TEXTURE0);

    if (lowRes) {
        prof.gpu("upscale");
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, fbW, fbH);
        glDisable(GL_DEPTH_TEST);
        glBindVertexArray(a.emptyVao);
        glBindTexture(GL_TEXTURE_2D, a.lowRes->colorTex());
        a.upscaleShader->use();
        a.upscaleShader->set("uSource", 0);
        a.upscaleShader->setVec2("uSourceSize", static_cast<float>(rw), static_cast<float>(rh));
        a.upscaleShader->set("uSharpen", clampf((1.0f - scale) * 1.2f, 0.0f, 0.5f));
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }

    prof.gpu("hud");
    if (a.showHud) drawHud(a, fbW, fbH);
}

// ----------------------------------------------------------------- shutdown --

void shutdown(App& a)
{
    // GL objects must be freed while the context still exists.
    a.minimap.reset();   // waits for any chart still painting from the world
    a.terrain.reset();
    a.world.reset();
    a.ocean.reset();
    a.scene.reset();
    a.lowRes.reset();
    a.boatMesh.reset();
    a.lighthouseMesh.reset();
    a.jettyMesh.reset();
    a.hutMesh.reset();
    a.beamMesh.reset();
    a.props.reset();
    a.hud.reset();
    a.text.reset();
    a.prof.reset();
    a.skyShader.reset();
    a.terrainShader.reset();
    a.objectShader.reset();
    a.propShader.reset();
    a.waterShader.reset();
    a.compositeShader.reset();
    a.beamShader.reset();
    a.hudShader.reset();
    a.minimapShader.reset();
    a.textShader.reset();
    a.upscaleShader.reset();
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
        "  M          minimap zoom\n"
        "  Backspace  respawn by an island        N          new world\n"
        "  Tab        release/capture mouse       H          hide HUD       F  wireframe\n"
        "  R          reload shaders              P          screenshot     Esc  quit\n"
        "  ` or F1    developer console (type help)  F3         performance overlay\n"
        "  F11        fullscreen (also Alt+Enter)\n\n");
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

// ----------------------------------------------------------------- console --
//
// Commands for testing by hand. Each one prints what it did, and most show
// the current value when given no arguments.

const char* islandName(IslandType t)
{
    switch (t) {
    case IslandType::Mountain:  return "mountain";
    case IslandType::Atoll:     return "atoll";
    case IslandType::SeaStacks: return "stacks";
    case IslandType::Cay:       return "cay";
    case IslandType::None:      break;
    }
    return "none";
}

// For sentences: "an atoll", "a cay".
const char* islandPhrase(IslandType t)
{
    switch (t) {
    case IslandType::Mountain:  return "a mountain island";
    case IslandType::Atoll:     return "an atoll";
    case IslandType::SeaStacks: return "the sea stacks";
    case IslandType::Cay:       return "a cay";
    case IslandType::None:      break;
    }
    return "open sea";
}

bool parseIslandType(const std::string& s, IslandType& out)
{
    std::string t = Console::lower(s);
    if (t == "mountain") out = IslandType::Mountain;
    else if (t == "atoll") out = IslandType::Atoll;
    else if (t == "stacks" || t == "seastacks") out = IslandType::SeaStacks;
    else if (t == "cay") out = IslandType::Cay;
    else if (t == "any") out = IslandType::None;
    else return false;
    return true;
}

// Islands within `rings` cells of (x, z), nearest first.
std::vector<IslandInfo> islandsNear(const IslandGenerator& w, double x, double z, IslandType kind, int rings)
{
    const double cell = IslandGenerator::kCellSize;
    const int64_t cx = static_cast<int64_t>(std::floor(x / cell)), cz = static_cast<int64_t>(std::floor(z / cell));
    std::vector<IslandInfo> out;
    for (int64_t j = cz - rings; j <= cz + rings; ++j)
        for (int64_t i = cx - rings; i <= cx + rings; ++i) {
            IslandInfo is = w.islandInCell(i, j);
            if (is.type != IslandType::None && (kind == IslandType::None || is.type == kind)) out.push_back(is);
        }
    auto d2 = [&](const IslandInfo& is) { return (is.cx - x) * (is.cx - x) + (is.cz - z) * (is.cz - z); };
    std::sort(out.begin(), out.end(), [&](const IslandInfo& p, const IslandInfo& q) { return d2(p) < d2(q); });
    return out;
}

const char* compassPoint(float deg)
{
    static const char* const names[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    int i = static_cast<int>(std::floor((deg + 22.5f) / 45.0f));
    return names[((i % 8) + 8) % 8];
}

// Move the boat (and the floating origin) to an exact spot, stopped.
void teleport(App& a, double x, double z, float heading)
{
    a.boat.placeAt(x, z, heading);
    a.originX = std::floor(x / kRebaseStep) * kRebaseStep;
    a.originZ = std::floor(z / kRebaseStep) * kRebaseStep;
    a.freeCam = false;
    a.chase.reset();
}

// "250" -> 250; "~" -> current; "~-40" -> current - 40 (relative, like Minecraft).
bool coordinate(Console& c, const std::string& s, double current, double& out)
{
    if (!s.empty() && s[0] == '~') {
        double d = 0.0;
        if (s.size() > 1 && !c.number(s.substr(1), d, "tp")) return false;
        out = current + d;
        return true;
    }
    return c.number(s, out, "tp");
}

std::string clockString(float hours)
{
    int hh = static_cast<int>(hours), mm = static_cast<int>((hours - static_cast<float>(hh)) * 60.0f);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02d:%02d", hh % 24, mm);
    return buf;
}

struct TuneVar {
    const char* name;
    float BoatTuning::*value;
    const char* help;
};

const TuneVar kTuneVars[] = {
    {"thrust",     &BoatTuning::thrust,      "acceleration at full throttle (m/s^2)"},
    {"boost",      &BoatTuning::boostMul,    "thrust multiplier while boosting"},
    {"reverse",    &BoatTuning::reverseMul,  "thrust multiplier going astern"},
    {"drag",       &BoatTuning::linearDrag,  "linear drag (1/s)"},
    {"quaddrag",   &BoatTuning::quadDrag,    "quadratic drag (1/m): sets top speed"},
    {"grip",       &BoatTuning::sideGrip,    "how fast sideways drift dies (1/s)"},
    {"turn",       &BoatTuning::turnRate,    "turn rate at speed (rad/s)"},
    {"idleturn",   &BoatTuning::idleTurn,    "fraction of turn rate when stopped"},
    {"spool",      &BoatTuning::engineSpool, "how fast the engine answers the throttle (1/s)"},
};

void registerCommands(App& a)
{
    auto add = [&a](const char* cat, const char* name, const char* usage, const char* help,
                    std::function<void(const ConsoleArgs&)> fn, std::vector<std::string> options = {}) {
        a.console.add({name, cat, usage, help, std::move(fn), std::move(options)});
    };
    auto onOffText = [](bool on) { return on ? "on" : "off"; };

    // ---- General -----------------------------------------------------------
    add("General", "help", "[command|all]", "list commands, or explain one", [&a](const ConsoleArgs& args) {
        Console& c = a.console;
        static const char* const order[] = {"General", "Boat", "World", "Time", "Camera", "Display"};
        if (args.empty()) {
            c.heading("Commands  (help <command> for details, help all for one line each)");
            for (const char* cat : order) {
                std::string names;
                for (const ConsoleCommand& cmd : c.commands())
                    if (cmd.category == cat) names += "  " + cmd.name;
                c.print("  %-8s%s", cat, names.c_str());
            }
            c.print("Several at once: time 6; view high.  Coordinates can be relative: tp ~200 ~");
            return;
        }
        if (Console::lower(args[0]) == "all") {
            for (const char* cat : order) {
                c.heading("%s", cat);
                for (const ConsoleCommand& cmd : c.commands()) {
                    if (cmd.category != cat) continue;
                    std::string head = cmd.name + " " + cmd.usage;
                    c.print("  %-28s %s", head.c_str(), cmd.help.c_str());
                }
            }
            return;
        }
        const ConsoleCommand* cmd = c.find(args[0]);
        if (!cmd) {
            c.error("No command '%s'", args[0].c_str());
            return;
        }
        c.heading("%s %s", cmd->name.c_str(), cmd->usage.c_str());
        c.print("  %s", cmd->help.c_str());
        if (!cmd->options.empty()) {
            std::string opts;
            for (const std::string& o : cmd->options) opts += " " + o;
            c.print("  options:%s", opts.c_str());
        }
    });
    add("General", "clear", "", "clear the console (also Ctrl+L)", [&a](const ConsoleArgs&) { a.console.clear(); });
    add("General", "echo", "<text>", "print text (handy in exec files)", [&a](const ConsoleArgs& args) {
        std::string s;
        for (const std::string& w : args) s += (s.empty() ? "" : " ") + w;
        a.console.print("%s", s.c_str());
    });
    add("General", "exec", "<file>", "run the commands in a text file, one per line", [&a](const ConsoleArgs& args) {
        if (args.empty()) { a.console.error("usage: exec <file>"); return; }
        if (!a.console.execFile(args[0])) a.console.error("exec: can't open '%s'", args[0].c_str());
    });
    add("General", "quit", "", "close the game", [&a](const ConsoleArgs&) { glfwSetWindowShouldClose(a.window, GLFW_TRUE); });

    // ---- Boat --------------------------------------------------------------
    add("Boat", "tp", "<x> <z> | island [type]", "teleport to world metres (~ = relative), or to the next island",
        [&a](const ConsoleArgs& args) {
            Console& c = a.console;
            if (!args.empty() && Console::lower(args[0]) == "island") {
                IslandType kind = IslandType::None;
                if (args.size() > 1 && !parseIslandType(args[1], kind)) {
                    c.error("tp island: unknown type '%s' (mountain, atoll, stacks, cay, any)", args[1].c_str());
                    return;
                }
                // Nearest island of that kind that we're not already at.
                for (const IslandInfo& is : islandsNear(*a.world, a.boat.x(), a.boat.z(), kind, 6)) {
                    double dist = std::hypot(is.cx - a.boat.x(), is.cz - a.boat.z());
                    if (dist < is.radius * 2.0 + 300.0) continue;
                    spawnBoat(a, is.cx, is.cz, is.type);
                    a.freeCam = false;
                    c.print("Sailed to %s %.1f km away, at (%.0f, %.0f)", islandPhrase(is.type), dist / 1000.0, is.cx,
                            is.cz);
                    return;
                }
                c.error("tp island: no %s island within 9 km", kind == IslandType::None ? "other" : islandName(kind));
                return;
            }
            if (args.size() != 2) {
                c.error("usage: tp <x> <z>   or   tp island [mountain|atoll|stacks|cay]");
                return;
            }
            double x, z;
            if (!coordinate(c, args[0], a.boat.x(), x) || !coordinate(c, args[1], a.boat.z(), z)) return;
            teleport(a, x, z, a.boat.heading());
            float ground = a.world->height(x, z);
            c.print("Teleported to (%.0f, %.0f)", x, z);
            if (ground > -1.5f && !a.boat.ghost)
                c.error("That's on land (%.0f m). 'ghost on' to sail off, or 'respawn'.", ground);
        },
        {"island"});
    add("Boat", "pos", "", "where am I: position, heading, speed, depth, nearest island", [&a](const ConsoleArgs&) {
        Console& c = a.console;
        double x = a.boat.x(), z = a.boat.z();
        c.print("Position  x %.1f  z %.1f m   (floating origin %.0f, %.0f)", x, z, a.originX, a.originZ);
        float hdg = bearingDegrees(a.boat.forward());
        c.print("Heading   %03.0f (%s)   speed %.2f m/s (%.1f kn)   depth %.1f m", hdg, compassPoint(hdg),
                a.boat.speed(), a.boat.speed() * 1.944f, -a.world->height(x, z));
        auto near = islandsNear(*a.world, x, z, IslandType::None, 2);
        if (!near.empty()) {
            const IslandInfo& is = near[0];
            float b = bearingDegrees(Vec3{static_cast<float>(is.cx - x), 0.0f, static_cast<float>(is.cz - z)});
            c.print("Nearest island: %s, %.2f km %s", islandName(is.type), std::hypot(is.cx - x, is.cz - z) / 1000.0,
                    compassPoint(b));
        }
    });
    add("Boat", "heading", "<degrees>", "point the boat at a compass bearing (0 = north, 90 = east)",
        [&a](const ConsoleArgs& args) {
            Console& c = a.console;
            if (args.empty()) {
                c.print("heading %03.0f", bearingDegrees(a.boat.forward()));
                return;
            }
            float deg;
            if (!c.number(args[0], deg, "heading")) return;
            float b = radians(deg);
            a.boat.setHeading(std::atan2(-std::cos(b), std::sin(b)));  // bearing -> (cos h, sin h) = (sin b, -cos b)
            c.print("heading %03.0f", bearingDegrees(a.boat.forward()));
        });
    add("Boat", "speed", "[m/s]", "show or set the boat's speed (drag then takes over)", [&a](const ConsoleArgs& args) {
        Console& c = a.console;
        if (!args.empty()) {
            float v;
            if (!c.number(args[0], v, "speed")) return;
            a.boat.setSpeed(clampf(v, -20.0f, 100.0f));
        }
        c.print("speed %.2f m/s (%.1f kn)", a.boat.speed(), a.boat.speed() * 1.944f);
    });
    add("Boat", "ghost", "[on|off]", "sail straight through land", [&a, onOffText](const ConsoleArgs& args) {
        bool on;
        if (!a.console.onOff(args, a.boat.ghost, on)) return;
        a.boat.ghost = on;
        a.console.print("ghost %s", onOffText(on));
    }, {"on", "off"});
    {
        std::vector<std::string> names{"reset"};
        for (const TuneVar& t : kTuneVars) names.push_back(t.name);
        add("Boat", "tune", "[name value | reset]", "show or change boat handling live", [&a](const ConsoleArgs& args) {
            Console& c = a.console;
            const BoatTuning defaults;
            if (args.size() == 1 && Console::lower(args[0]) == "reset") {
                a.boat.tuning = defaults;
                c.print("Boat handling reset to defaults");
                return;
            }
            if (args.size() == 2) {
                for (const TuneVar& t : kTuneVars) {
                    if (Console::lower(args[0]) != t.name) continue;
                    float v;
                    if (!c.number(args[1], v, "tune")) return;
                    a.boat.tuning.*t.value = std::max(0.0f, v);
                    c.print("%s = %g  (default %g)", t.name, a.boat.tuning.*t.value, defaults.*t.value);
                    return;
                }
                c.error("tune: no setting '%s'", args[0].c_str());
                return;
            }
            if (!args.empty()) {
                c.error("usage: tune   |   tune <name> <value>   |   tune reset");
                return;
            }
            c.heading("Boat handling (tune <name> <value>)");
            for (const TuneVar& t : kTuneVars)
                c.print("  %-9s %7.3f   default %7.3f   %s", t.name, a.boat.tuning.*t.value, defaults.*t.value, t.help);
        }, names);
    }
    add("Boat", "respawn", "", "move the boat to open water by the nearest island (Backspace)",
        [&a](const ConsoleArgs&) {
            spawnBoat(a, a.boat.x(), a.boat.z());
            a.freeCam = false;
            a.console.print("Respawned at (%.0f, %.0f)", a.boat.x(), a.boat.z());
        });

    // ---- World -------------------------------------------------------------
    add("World", "seed", "[number]", "show the world seed, or rebuild the world from one", [&a](const ConsoleArgs& args) {
        Console& c = a.console;
        if (!args.empty()) {
            double v;
            if (!c.number(args[0], v, "seed")) return;
            if (v < 0.0 || v > 4294967295.0) {
                c.error("seed: must be 0 .. 4294967295");
                return;
            }
            rebuildWorld(a, static_cast<uint32_t>(v));
            spawnBoat(a, a.boat.x(), a.boat.z());
        }
        c.print("seed %u", a.world->seed());
    });
    add("World", "newworld", "", "a new world from a random seed (N)", [&a](const ConsoleArgs&) { newWorld(a); });
    add("World", "viewdist", "[chunks]", "terrain view radius in 128 m chunks (8..64)", [&a](const ConsoleArgs& args) {
        Console& c = a.console;
        if (!args.empty()) {
            float v;
            if (!c.number(args[0], v, "viewdist")) return;
            a.opt.radius = std::clamp(static_cast<int>(v), 8, 64);
            a.terrain.reset();  // stop its workers before making the new one
            a.terrain = std::make_unique<TerrainStreamer>(*a.world, a.opt.radius);
        }
        c.print("viewdist %d chunks (%.1f km)", a.opt.radius, a.terrain->viewDistance() / 1000.0f);
    });
    add("World", "waves", "[scale]", "wave height: 0 calm, 1 normal, up to 3 stormy", [&a](const ConsoleArgs& args) {
        Console& c = a.console;
        if (!args.empty()) {
            float v;
            if (!c.number(args[0], v, "waves")) return;
            a.ocean->setWaveScale(v);
        }
        c.print("waves %.2f", a.ocean->waveScale());
    });
    add("World", "fog", "[density|reset]", "fog thickness (default 0.00024, 0 = none)", [&a](const ConsoleArgs& args) {
        Console& c = a.console;
        if (!args.empty()) {
            float v = kDefaultFog;
            if (Console::lower(args[0]) != "reset" && !c.number(args[0], v, "fog")) return;
            a.fogDensity = clampf(v, 0.0f, 0.02f);
        }
        c.print("fog %g", a.fogDensity);
    }, {"reset"});

    // ---- Time --------------------------------------------------------------
    add("Time", "time", "[hh[:mm]]", "show or set the time of day (e.g. time 18:30)", [&a](const ConsoleArgs& args) {
        Console& c = a.console;
        if (!args.empty()) {
            const std::string& t = args[0];
            size_t colon = t.find(':');
            float h = 0.0f, m = 0.0f;
            if (!c.number(t.substr(0, colon), h, "time")) return;
            if (colon != std::string::npos && !c.number(t.substr(colon + 1), m, "time")) return;
            a.env.hours = std::fmod(std::fmod(h + m / 60.0f, 24.0f) + 24.0f, 24.0f);
        }
        c.print("time %s%s", clockString(a.env.hours).c_str(), a.env.paused ? " (clock paused)" : "");
    });
    add("Time", "pause", "[on|off]", "stop or start the clock (T)", [&a](const ConsoleArgs& args) {
        bool on;
        if (!a.console.onOff(args, a.env.paused, on)) return;
        a.env.paused = on;
        a.console.print("clock %s", on ? "paused" : "running");
    }, {"on", "off"});
    add("Time", "daylength", "[minutes]", "real minutes for a full day (default 16)", [&a](const ConsoleArgs& args) {
        Console& c = a.console;
        if (!args.empty()) {
            float v;
            if (!c.number(args[0], v, "daylength")) return;
            a.env.dayLengthMinutes = clampf(v, 0.5f, 1440.0f);
        }
        c.print("daylength %g minutes", a.env.dayLengthMinutes);
    });

    // ---- Camera ------------------------------------------------------------
    add("Camera", "view", "<preset>", "camera preset around the boat", [&a](const ConsoleArgs& args) {
        if (args.empty()) { a.console.error("usage: view chase|side|high|low|cove"); return; }
        a.freeCam = false;
        applyView(a, Console::lower(args[0]));
        a.console.print("view %s", args[0].c_str());
    }, {"chase", "side", "high", "low", "cove"});
    add("Camera", "freecam", "[on|off]", "fly the camera freely (V)", [&a, onOffText](const ConsoleArgs& args) {
        bool on;
        if (!a.console.onOff(args, a.freeCam, on)) return;
        a.freeCam = on;
        if (!on) a.chase.reset();
        a.console.print("freecam %s", onOffText(on));
    }, {"on", "off"});
    add("Camera", "fov", "[degrees]", "vertical field of view (default 60)", [&a](const ConsoleArgs& args) {
        Console& c = a.console;
        if (!args.empty()) {
            float v;
            if (!c.number(args[0], v, "fov")) return;
            a.cam.fovY = radians(clampf(v, 15.0f, 120.0f));
        }
        c.print("fov %.0f", a.cam.fovY * 180.0f / kPi);
    });
    add("Camera", "zoom", "[metres]", "camera distance from the boat (7..60, mouse wheel)", [&a](const ConsoleArgs& args) {
        Console& c = a.console;
        if (!args.empty()) {
            float v;
            if (!c.number(args[0], v, "zoom")) return;
            a.chase.distance = clampf(v, 7.0f, 60.0f);
        }
        c.print("zoom %.1f m", a.chase.distance);
    });

    // ---- Display -----------------------------------------------------------
    add("Display", "hud", "[on|off]", "show or hide the HUD (H)", [&a, onOffText](const ConsoleArgs& args) {
        bool on;
        if (!a.console.onOff(args, a.showHud, on)) return;
        a.showHud = on;
        a.console.print("hud %s", onOffText(on));
    }, {"on", "off"});
    add("Display", "wireframe", "[on|off]", "draw terrain and sea as wireframe (F)", [&a, onOffText](const ConsoleArgs& args) {
        bool on;
        if (!a.console.onOff(args, a.wireframe, on)) return;
        a.wireframe = on;
        a.console.print("wireframe %s", onOffText(on));
    }, {"on", "off"});
    add("Display", "lights", "[on|off]", "boat lamps and spotlight (L)", [&a, onOffText](const ConsoleArgs& args) {
        bool on;
        if (!a.console.onOff(args, a.boatLights, on)) return;
        a.boatLights = on;
        a.console.print("lights %s", onOffText(on));
    }, {"on", "off"});
    add("Display", "minimap", "[400|1000|2500]", "minimap range in metres (M cycles)", [&a](const ConsoleArgs& args) {
        Console& c = a.console;
        if (args.empty()) {
            a.minimap->cycleZoom();
        } else {
            float v;
            if (!c.number(args[0], v, "minimap")) return;
            int best = 0;  // nearest of the three ranges
            for (int i = 1; i < 3; ++i)
                if (std::fabs(Minimap::kRanges[i] - v) < std::fabs(Minimap::kRanges[best] - v)) best = i;
            a.minimap->setZoom(best);
        }
        c.print("minimap %.0f m", a.minimap->range());
    }, {"400", "1000", "2500"});
    add("Display", "reload", "", "reload shaders from disk (R)", [&a](const ConsoleArgs&) {
        int failed = 0;
        for (Shader* s : a.allShaders())
            if (!s->reload()) ++failed;
        if (failed) a.console.error("%d shader(s) failed to compile; the old ones are still in use (see terminal)", failed);
        else a.console.print("Shaders reloaded");
    });
    add("Display", "screenshot", "[file.bmp]", "save the next frame, without the console (P)", [&a](const ConsoleArgs& args) {
        std::string name = args.empty() ? nextShotName(a) : args[0];
        if (name.size() < 4 || Console::lower(name.substr(name.size() - 4)) != ".bmp") name += ".bmp";
        a.pendingShot = name;
    });
    add("Display", "renderscale", "[0.25..1 | auto]", "draw the 3D view at a fraction of the screen resolution",
        [&a](const ConsoleArgs& args) {
            Console& c = a.console;
            if (!args.empty()) {
                if (Console::lower(args[0]) == "auto") {
                    a.renderScale = 0.0f;
                } else {
                    float v;
                    if (!c.number(args[0], v, "renderscale")) return;
                    a.renderScale = clampf(v, 0.25f, 1.0f);
                }
            }
            int fw, fh;
            glfwGetFramebufferSize(a.window, &fw, &fh);
            float sc = effectiveScale(a, fh);
            c.print("renderscale %s: 3D view at %dx%d of %dx%d", a.renderScale > 0.0f ? strf("%.2f", sc).c_str()
                    : strf("auto (%.2f)", sc).c_str(), static_cast<int>(std::lround(static_cast<float>(fw) * sc)),
                    static_cast<int>(std::lround(static_cast<float>(fh) * sc)), fw, fh);
        }, {"auto", "0.5", "0.75", "1"});
    add("Display", "vsync", "[on|off]", "wait for the screen's refresh (no tearing)", [&a](const ConsoleArgs& args) {
        bool on;
        if (!a.console.onOff(args, a.vsync, on)) return;
        a.vsync = on;
        applyVsync(a);
        a.console.print("vsync %s", on ? "on" : "off");
    }, {"on", "off"});
    add("Display", "fullscreen", "[on|off]", "fullscreen on the current monitor (F11 or Alt+Enter)",
        [&a](const ConsoleArgs& args) {
            bool on;
            if (!a.console.onOff(args, a.fullscreen, on)) return;
            setFullscreen(a, on);
            a.console.print("fullscreen %s", on ? "on" : "off");
        }, {"on", "off"});
    add("Display", "perf", "[on|off|report]", "performance overlay (F3); report prints the numbers",
        [&a](const ConsoleArgs& args) {
            Console& c = a.console;
            if (!args.empty() && Console::lower(args[0]) == "report") {
                const Profiler& p = *a.prof;
                c.heading("%s", a.gpuName.c_str());
                c.print("frame %.2f ms avg, %.2f ms worst (last 4 s)", p.frameAvg(), p.frameMax());
                c.print("CPU %.2f ms", p.cpuTotal());
                for (const Profiler::Section& sct : p.cpuSections())
                    c.print("  %-16s avg %6.2f  max %6.2f", sct.name.c_str(), sct.avg(), sct.max());
                c.print("GPU %.2f ms", p.gpuTotal());
                for (const Profiler::Section& sct : p.gpuSections())
                    c.print("  %-16s avg %6.2f  max %6.2f", sct.name.c_str(), sct.avg(), sct.max());
                return;
            }
            bool on;
            if (!c.onOff(args, a.showPerf, on)) return;
            a.showPerf = on;
            c.print("perf %s", on ? "on" : "off");
        }, {"on", "off", "report"});
    add("Display", "stats", "", "frame rate, terrain chunks, triangles, props", [&a](const ConsoleArgs&) {
        Console& c = a.console;
        const ChunkStats& st = a.terrain->stats();
        c.print("%.0f fps | chunks %d drawn / %d loaded, %d queued, %d building", a.fps, st.drawn, st.loaded, st.queued,
                st.building);
        c.print("%.2fM triangles | %zu props | %zu landmarks | view %.1f km", static_cast<double>(st.triangles) / 1e6, st.props,
                a.landmarks.all().size(), a.terrain->viewDistance() / 1000.0f);
    });
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
    glfwWindowHint(GLFW_SAMPLES, app.opt.msaa);
    const bool shotMode = !app.opt.shot.empty();
    if (shotMode) glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

    app.window = glfwCreateWindow(app.opt.width, app.opt.height, "SeaSail", nullptr, nullptr);
    if (!app.window) {
        std::fprintf(stderr, "Failed to create an OpenGL 3.3 window\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(app.window);
    app.renderScale = app.opt.scale > 0.0f ? clampf(app.opt.scale, 0.25f, 1.0f) : 0.0f;
    applyVsync(app);
    glfwGetWindowPos(app.window, &app.winX, &app.winY);
    glfwGetWindowSize(app.window, &app.winW, &app.winH);
    if (app.opt.fullscreen && !shotMode) setFullscreen(app, true);
    glfwSetKeyCallback(app.window, onKey);
    glfwSetScrollCallback(app.window, onScroll);
    glfwSetCharCallback(app.window, onChar);
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
    app.minimapShader   = load("minimap.vert", "minimap.frag");
    app.textShader      = load("text.vert", "text.frag");
    app.upscaleShader   = load("fullscreen.vert", "upscale.frag");
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
    app.lowRes = std::make_unique<ScreenTarget>();
    app.hud   = std::make_unique<Hud>();
    app.minimap = std::make_unique<Minimap>();
    app.text    = std::make_unique<TextRenderer>();
    app.prof    = std::make_unique<Profiler>();
    app.gpuName = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
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

    // Console: commands, then autoexec.cfg (if there is one in the working
    // folder), then any --cmd / --exec from the command line, in order.
    registerCommands(app);
    app.console.heading("SeaSail console. Type help for commands; ` or Esc closes it.");
    if (app.console.execFile("autoexec.cfg")) app.console.print("Ran autoexec.cfg");
    for (const std::string& line : app.opt.startup) app.console.execute(line);
    if (app.opt.console) {
        app.console.setOpen(true);
        app.console.update(1.0f);  // skip the slide-in
    }

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
        app.minimap->update(*app.world, app.boat.x(), app.boat.z());
        for (int i = 0; i < 400; ++i) {   // let the chart finish painting
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            app.minimap->update(*app.world, app.boat.x(), app.boat.z());
        }
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();

        int fw, fh;
        glfwGetFramebufferSize(app.window, &fw, &fh);
        if (app.opt.bench > 0) {
            // Time the same view for N frames; GPU results arrive a few frames late.
            Profiler& prof = *app.prof;
            for (int i = 0; i < app.opt.bench + 8; ++i) {
                prof.beginFrame();
                prof.cpu("draw calls");
                render(app, fw, fh);
                prof.gpuEnd();
                prof.cpuEnd();
                glFinish();
            }
            std::printf("Bench %dx%d, %d frames on %s\n", fw, fh, app.opt.bench, app.gpuName.c_str());
            float total = 0.0f;
            for (const Profiler::Section& s : prof.gpuSections()) {
                // Average over the frames actually measured (the ring holds 120).
                float sum = 0.0f;
                int n = std::min(app.opt.bench, Profiler::kHistory);
                for (int k = 0; k < n; ++k) sum += s.ms[static_cast<size_t>((s.lastFrame - k + Profiler::kHistory * 4) % Profiler::kHistory)];
                std::printf("  gpu %-12s %8.2f ms\n", s.name.c_str(), sum / static_cast<float>(n));
                total += sum / static_cast<float>(n);
            }
            std::printf("  gpu %-12s %8.2f ms\n", "TOTAL", total);
        }
        render(app, fw, fh);
        if (app.showPerf) drawPerf(app, fw, fh);
        if (app.console.isOpen())
            drawConsole(app.console, *app.hud, *app.text, *app.hudShader, *app.textShader, fw, fh);
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

        Profiler& prof = *app.prof;
        prof.beginFrame();
        prof.cpu("input + sim");
        glfwPollEvents();
        update(app, dt);
        prof.cpu("stream plan");
        app.terrain->update(camWorldX(app), app.cam.position.y, camWorldZ(app));
        prof.cpu("stream upload");
        app.terrain->uploadFinished(3.0);  // ms per frame for GPU uploads
        prof.cpu("landmarks+map");
        app.landmarks.update(*app.world, camWorldX(app), camWorldZ(app));
        app.minimap->update(*app.world, app.boat.x(), app.boat.z());

        int fw, fh;
        glfwGetFramebufferSize(app.window, &fw, &fh);
        if (fw > 0 && fh > 0) {
            prof.cpu("draw calls");
            render(app, fw, fh);
            // Screenshots are taken here: a whole frame, before the console.
            if (!app.pendingShot.empty()) {
                if (saveBmp(app.pendingShot, fw, fh)) app.console.print("Saved %s", app.pendingShot.c_str());
                else app.console.error("Could not write %s", app.pendingShot.c_str());
                app.pendingShot.clear();
            }
            prof.cpu("overlays");
            prof.gpu("overlays");
            if (app.showPerf) drawPerf(app, fw, fh);
            drawConsole(app.console, *app.hud, *app.text, *app.hudShader, *app.textShader, fw, fh);
        }
        prof.gpuEnd();
        prof.cpuEnd();   // the rest of the frame is waiting for vsync
        glfwSwapBuffers(app.window);

        ++frames;
        if (now - titleTimer >= 0.5) {
            app.fps = static_cast<float>(frames / (now - titleTimer));
            const ChunkStats& st = app.terrain->stats();
            int hh = static_cast<int>(app.env.hours), mm = static_cast<int>((app.env.hours - hh) * 60.0f);
            char title[256];
            std::snprintf(title, sizeof(title),
                          "SeaSail | %.0f fps | %02d:%02d%s | %.1f kn | heading %03.0f | %.2f, %.2f km | chunks %d/%d | props %zu%s%s",
                          app.fps, hh, mm, app.env.paused ? " (paused)" : "",
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
