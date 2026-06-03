#include <emscripten.h>
#include <emscripten/html5.h>
#include <GLES3/gl3.h>
#include <cstdio>
#include <cmath>
#include <vector>

#include "../math/vecmath.h"
#include "../graphics/renderer2d.h"
#include "../world/planet.h"
#include "../entities/lander.h"
#include "../entities/cow.h"
#include "../input/input.h"
#include "debug.h"
#include "../graphics/shader.h"

// ============================================================================
// Fullscreen atmosphere + cloud shader passes, ported from the original game's
// atmosphere.js / clouds.js. Each runs after the 2D scene and reconstructs the
// world point under every pixel from the camera transform, then shades a
// per-planet slab: the atmosphere is a thin Gaussian "arc/ring" band lit by the
// sun (blue Rayleigh on the day side, warm Mie at the terminator); the clouds
// are an FBM density field in a low-altitude band with Beer's-law opacity and
// sun-direction shading. Up to SHADER_MAX_PLANETS bodies.
// ============================================================================
static constexpr int SHADER_MAX_PLANETS = 10;

static const char* kFsVert = R"(#version 300 es
layout(location = 0) in vec2 a_pos;
void main() { gl_Position = vec4(a_pos, 0.0, 1.0); }
)";

static const char* kAtmoFrag = R"(#version 300 es
precision highp float;
#define MAX_PLANETS 10
uniform vec2 u_resolution;
uniform vec2 u_viewFocus;
uniform vec2 u_screenCenter;
uniform float u_viewScale;
uniform float u_viewRotation;
uniform int u_planetCount;
uniform vec2 u_planetPos[MAX_PLANETS];
uniform float u_planetRadius[MAX_PLANETS];
uniform float u_atmoRadius[MAX_PLANETS];
uniform vec2 u_sunPos;
uniform float u_time;
uniform float u_rayleighH;
uniform float u_rimH;
uniform float u_mieH;
uniform float u_sunIntensity;
uniform float u_extinction;
uniform float u_overlay;        // planet-face sky fill (0 near the surface -> 1 in space)
uniform sampler2D u_terrainTex; // 2048x1: terrain radius per angle (the silhouette)
out vec4 fragColor;

#define TERRAIN_N 2048

// Terrain (surface) radius at a given outward direction, sampled from the
// silhouette LUT with linear wrap so the sky meets the ground exactly.
float terrainRadius(vec2 outward) {
  float ang = degrees(atan(outward.y, outward.x));
  if (ang < 0.0) ang += 360.0;
  float fidx = ang / 360.0 * float(TERRAIN_N);
  int i0 = int(fidx) % TERRAIN_N;
  int i1 = (i0 + 1) % TERRAIN_N;
  float frac = fract(fidx);
  return mix(texelFetch(u_terrainTex, ivec2(i0, 0), 0).r,
             texelFetch(u_terrainTex, ivec2(i1, 0), 0).r, frac);
}

// Scattering coefficients (relative), adapted from Maxime Heckel's sky article.
const vec3 BETA_R = vec3(0.0058, 0.0135, 0.0331);    // Rayleigh: blue scatters most
const vec3 BETA_M = vec3(0.0080);                    // Mie haze (achromatic; keep < blue so sky stays blue)
const vec3 BETA_O = vec3(0.00065, 0.00188, 0.00008); // ozone absorption

void main() {
  // Reconstruct the world point under this pixel (inverse camera transform).
  vec2 pixel = vec2(gl_FragCoord.x, u_resolution.y - gl_FragCoord.y);
  vec2 centered = pixel - u_screenCenter;
  float c = cos(-u_viewRotation), s = sin(-u_viewRotation);
  vec2 unrot = vec2(c * centered.x - s * centered.y, s * centered.x + c * centered.y);
  vec2 world = unrot / u_viewScale + u_viewFocus;

  // Composite color + opacity so the sky OCCLUDES the stars where it's bright
  // (premultiplied "over"): day side washes the stars out, night side keeps them.
  vec3 accumCol = vec3(0.0);
  float accumA = 0.0;
  for (int i = 0; i < MAX_PLANETS; i++) {
    if (i >= u_planetCount) break;
    vec2 pc = u_planetPos[i];
    vec2 d = world - pc;
    float baseR = u_planetRadius[i];      // base radius (sky-gradient reference)
    float atmoR = u_atmoRadius[i];        // top of the atmosphere
    float dist = length(d);
    if (dist >= atmoR) continue;          // outside the atmosphere disk -> space

    // Directional sun: parallel rays from the sun's direction, so the lit
    // hemisphere is the side facing the sun.
    vec2 outward = d / max(dist, 0.0001);
    vec2 toSun = normalize(u_sunPos - pc);
    float sunDot = dot(outward, toSun);
    float dayNight = smoothstep(-0.25, 0.30, sunDot);

    // The actual terrain line at this angle: above it is open sky, below it is
    // the planet face. It only controls GATING (not color), so it never shows as
    // a color seam — at the surface it's where sky meets ground; from space the
    // face fills in with the very same sky and the boundary disappears.
    float surfaceR = terrainRadius(outward);

    // ---- ONE sky color for the whole disk -----------------------------------
    // Radial position through the atmosphere: 0 at/below the surface, 1 at the
    // top. Over the planet face it pins to 0 (the surface sky continued inward).
    // Rayleigh blue is two bands: surface-dense (u_rayleighH) + inverted rim-dense
    // (u_rimH); Mie haze hugs the surface.
    float h = clamp((dist - baseR) / max(atmoR - baseR, 0.0001), 0.0, 1.0);
    float densR = exp(-h * u_rayleighH) + exp(-(1.0 - h) * u_rimH);
    float densM = exp(-h * u_mieH);

    // Air-mass of the sunlight (~1 overhead, large at the terminator); extinction
    // reddens it so sunsets *emerge*.
    float airmass = 1.0 / max(sunDot * 0.9 + 0.1, 0.05);
    vec3 extBeta = BETA_R + BETA_M + BETA_O;
    vec3 sunlight = exp(-extBeta * max(airmass - 1.0, 0.0) * u_extinction);

    // Sun glint + warm sunset band are limb-only (faded out toward the disk
    // centre by limbW) so they never smear a streak across the flat planet face.
    // They belong to the open-sky shell ABOVE the terrain only: over the planet
    // face (incl. mountain tops, which have a small nonzero h) we force limbW to 0
    // so the bright sunset band can't light up the peaks like snow at dusk.
    float limbW = smoothstep(0.05, 0.45, h);
    if (dist < surfaceR) limbW = 0.0;
    float mieGlow = 1.0 + 2.0 * pow(max(sunDot, 0.0), 16.0) * limbW;
    float termBand = exp(-pow(sunDot * 2.0, 2.0) * 4.0) * limbW;
    vec3 rayleigh = BETA_R * densR;
    vec3 mie = BETA_M * densM * (mieGlow * 0.4 + termBand * 1.6 + 0.25);

    vec3 col = u_sunIntensity * sunlight * (rayleigh + mie) * dayNight;
    float opacity = dayNight * (densR * 0.8 + densM * 0.6) * 1.8;

    // Night side: a faint, almost-transparent dark blue so the shell stays
    // visible against space instead of going fully black.
    float night = 1.0 - dayNight;
    col += vec3(0.03, 0.06, 0.14) * densR * night;
    opacity += night * clamp(densR, 0.0, 1.0) * 0.22;
    opacity = clamp(opacity, 0.0, 1.0);

    // Below the terrain line = planet face: only fill it with sky once you've
    // climbed out toward space, so the ground stays visible from up close.
    if (dist < surfaceR) {
      col *= u_overlay;
      opacity *= u_overlay;
    }

    accumCol += min(col, vec3(1.3)) * (1.0 - accumA);
    accumA   += opacity * (1.0 - accumA);
  }
  fragColor = vec4(accumCol, accumA);   // premultiplied; drawn with GL_ONE, GL_ONE_MINUS_SRC_ALPHA
}
)";

static const char* kCloudFrag = R"(#version 300 es
precision highp float;
#define MAX_PLANETS 10
uniform vec2 u_resolution;
uniform vec2 u_viewFocus;
uniform vec2 u_screenCenter;
uniform float u_viewScale;
uniform float u_viewRotation;
uniform float u_time;
uniform int u_planetCount;
uniform vec2 u_planetPos[MAX_PLANETS];
uniform float u_planetOuterR[MAX_PLANETS];
uniform vec2 u_sunPos;
uniform float u_coverage;   // 0 = clear sky, 1 = overcast
uniform float u_falloff;    // how much coverage thins with altitude (0 = none, 1 = clears up high)
uniform float u_overlay;    // cloud deck over the planet disk (0 near surface -> 1 in space)
uniform float u_nightDarkness; // brightness of clouds on the deepest night side
uniform float u_softness;      // terminator half-width in sunDot units (matches the body)
uniform sampler2D u_terrainTex; // 2048x1: terrain radius per angle (the silhouette)
out vec4 fragColor;

const float CLOUD_DRIFT = 0.004;

#define TERRAIN_N 2048
// Terrain radius at a given outward direction (matches the atmosphere shader), so
// the cloud deck meets the planet silhouette exactly instead of a smaller circle.
float terrainRadius(vec2 outward) {
  float ang = degrees(atan(outward.y, outward.x));
  if (ang < 0.0) ang += 360.0;
  float fidx = ang / 360.0 * float(TERRAIN_N);
  int i0 = int(fidx) % TERRAIN_N;
  int i1 = (i0 + 1) % TERRAIN_N;
  float frac = fract(fidx);
  return mix(texelFetch(u_terrainTex, ivec2(i0, 0), 0).r,
             texelFetch(u_terrainTex, ivec2(i1, 0), 0).r, frac);
}

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float vnoise(vec2 p) {
  vec2 i = floor(p), f = fract(p);
  vec2 u = f * f * (3.0 - 2.0 * f);
  return mix(mix(hash(i), hash(i + vec2(1.0, 0.0)), u.x),
             mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), u.x), u.y);
}
float fbm(vec2 p) {
  float v = 0.0, a = 0.5;
  for (int i = 0; i < 4; i++) { v += a * vnoise(p); p *= 2.1; a *= 0.5; }
  return v;
}

void main() {
  vec2 pixel = vec2(gl_FragCoord.x, u_resolution.y - gl_FragCoord.y);
  vec2 centered = pixel - u_screenCenter;
  float c = cos(-u_viewRotation), s = sin(-u_viewRotation);
  vec2 unrot = vec2(c * centered.x - s * centered.y, s * centered.x + c * centered.y);
  vec2 world = unrot / u_viewScale + u_viewFocus;

  vec4 accum = vec4(0.0);
  for (int i = 0; i < MAX_PLANETS; i++) {
    if (i >= u_planetCount) break;
    vec2 d = world - u_planetPos[i];
    float dist = length(d);
    float outerR = u_planetOuterR[i];
    if (dist >= outerR) continue;
    // The disk (deck over the planet face) reaches the actual terrain silhouette,
    // so it lines up with the atmosphere face instead of stopping at a circle.
    vec2 outwardDir = d / max(dist, 0.0001);
    float surfaceR = terrainRadius(outwardDir);
    bool inDisk = dist <= surfaceR;
    // Over the planet face the cloud band has no thickness to traverse, so we
    // render it as a deck seen from above, faded in with altitude (u_overlay) so
    // the clouds continue across the planet face only once you're out in space.
    if (inDisk && u_overlay <= 0.0) continue;

    // Use the actual terrain silhouette as the handoff point between the
    // top-down cloud deck over the planet face and the edge-on atmospheric band.
    // They intentionally overlap around that line, which prevents a detached
    // "cloud disk plus separate cloud ring" look when viewed from space.
    float shellSpan = max(outerR - surfaceR, 1.0);
    float signedSurfaceAlt = dist - surfaceR;
    float shellHRaw = signedSurfaceAlt / shellSpan;
    float shellH = clamp(shellHRaw, 0.0, 1.0);
    float edgeFeather = max(shellSpan * 0.06, 32.0);
    float edgeT = smoothstep(-edgeFeather, edgeFeather, signedSurfaceAlt);
    float h = mix(0.5, shellH, edgeT);

    float deckDensity = u_overlay * (1.0 - smoothstep(0.0, edgeFeather, signedSurfaceAlt));
    float shellBand = smoothstep(-0.06, 0.22, shellHRaw) *
                      (1.0 - smoothstep(0.82, 1.0, shellH));
    float altDensity = max(deckDensity, shellBand);
    vec2 local = d;
    float density = fbm(local * 0.0008 + vec2(u_time * CLOUD_DRIFT, u_time * CLOUD_DRIFT * 0.4));
    // Large-scale, slowly-drifting "weather" field so coverage isn't uniform
    // around the planet: big clear patches and overcast regions. u_coverage is
    // the average; the weather term pushes local coverage above/below it.
    float weather = fbm(local * 0.00011 + vec2(u_time * CLOUD_DRIFT * 0.18, 7.3));
    weather = smoothstep(0.30, 0.72, weather);
    float localCoverage = clamp(u_coverage * (0.25 + 1.6 * weather), 0.0, 1.0);
    // Coverage peaks in the mid cloud band and thins toward BOTH the surface and
    // the top of the band, so clouds don't pile up on the ground or up high.
    // u_falloff sets how strongly the two edges thin out.
    float bump = 4.0 * h * (1.0 - h);   // 1.0 at mid-band, 0.0 at both edges
    localCoverage *= mix(1.0, bump, u_falloff);
    // Coverage shifts the noise threshold: high coverage lets more of the FBM
    // field through (overcast); low coverage keeps only the densest puffs.
    float lo = mix(0.78, 0.22, localCoverage);
    density = smoothstep(lo, lo + 0.24, density) * altDensity;
    if (density <= 0.0) continue;

    float alpha = 1.0 - exp(-density * 3.5);
    vec2 outward = d / max(dist, 0.0001);
    vec2 toSun = normalize(u_sunPos - u_planetPos[i]);   // directional sun
    float sunDot = dot(outward, toSun);
    float lit = smoothstep(-0.25, 0.45, sunDot);
    vec3 cloudColor = mix(vec3(0.45, 0.5, 0.6), vec3(1.0, 0.98, 0.94), lit);
    // Same day/night terminator as the planet body, so clouds on the dark side go
    // dark too instead of staying bright white.
    float dayNight = smoothstep(-u_softness, u_softness, sunDot);
    cloudColor *= mix(u_nightDarkness, 1.0, dayNight);

    // Premultiplied "over" composite (drawn with GL_ONE, GL_ONE_MINUS_SRC_ALPHA).
    accum.rgb += cloudColor * alpha * (1.0 - accum.a);
    accum.a   += alpha * (1.0 - accum.a);
  }
  fragColor = accum;
}
)";

// Day/night terminator over the planet body. A screen-space multiply pass (drawn
// with GL_ZERO, GL_SRC_COLOR) that darkens the hemisphere of the planet facing
// away from the sun, so the night side actually goes dark instead of relying on
// the sky dimming around it. This is the 2D equivalent of "shade by direction to
// the light" from Maxime Heckel's atmosphere/volumetric-light articles: the lit
// side stays full brightness, the far side falls toward u_nightDarkness. Outside
// the terrain silhouette it emits white (x1) so it leaves the sky/stars alone.
static const char* kTerminatorFrag = R"(#version 300 es
precision highp float;
#define MAX_PLANETS 10
uniform vec2 u_resolution;
uniform vec2 u_viewFocus;
uniform vec2 u_screenCenter;
uniform float u_viewScale;
uniform float u_viewRotation;
uniform int u_planetCount;
uniform vec2 u_planetPos[MAX_PLANETS];
uniform float u_planetMaxR[MAX_PLANETS];   // generous radius bound for early-out
uniform vec2 u_sunPos;
uniform float u_nightDarkness;             // brightness floor of the deepest night side
uniform float u_softness;                  // terminator half-width in sunDot units
uniform sampler2D u_terrainTex;            // 2048x1: terrain radius per angle (the silhouette)
out vec4 fragColor;

#define TERRAIN_N 2048
float terrainRadius(vec2 outward) {
  float ang = degrees(atan(outward.y, outward.x));
  if (ang < 0.0) ang += 360.0;
  float fidx = ang / 360.0 * float(TERRAIN_N);
  int i0 = int(fidx) % TERRAIN_N;
  int i1 = (i0 + 1) % TERRAIN_N;
  float frac = fract(fidx);
  return mix(texelFetch(u_terrainTex, ivec2(i0, 0), 0).r,
             texelFetch(u_terrainTex, ivec2(i1, 0), 0).r, frac);
}

void main() {
  vec2 pixel = vec2(gl_FragCoord.x, u_resolution.y - gl_FragCoord.y);
  vec2 centered = pixel - u_screenCenter;
  float c = cos(-u_viewRotation), s = sin(-u_viewRotation);
  vec2 unrot = vec2(c * centered.x - s * centered.y, s * centered.x + c * centered.y);
  vec2 world = unrot / u_viewScale + u_viewFocus;

  float shade = 1.0;   // x1 by default -> leaves everything outside the planet untouched
  for (int i = 0; i < MAX_PLANETS; i++) {
    if (i >= u_planetCount) break;
    vec2 d = world - u_planetPos[i];
    float dist = length(d);
    if (dist >= u_planetMaxR[i]) continue;     // clearly outside the body
    vec2 outward = d / max(dist, 0.0001);
    if (dist > terrainRadius(outward)) continue;  // above the terrain line = sky, not body

    // Directional sun: the lit hemisphere faces the sun. dayNight ramps across the
    // terminator band (width set by u_softness) so there's a soft dusk gradient.
    vec2 toSun = normalize(u_sunPos - u_planetPos[i]);
    float sunDot = dot(outward, toSun);
    float dayNight = smoothstep(-u_softness, u_softness, sunDot);
    shade = mix(u_nightDarkness, 1.0, dayNight);
  }
  fragColor = vec4(vec3(shade), 1.0);
}
)";

// ============================================================================
// Camera / view — ports updateView() from sketch.js: altitude-based framing,
// velocity-predictive follow, and an auto-roll that keeps gravity pointing
// screen-down so you stay upright wherever you are on the planet.
// ============================================================================
struct View {
    float focusX = 0, focusY = 0;
    float scale = 1.0f;
    float rotation = 0.0f; // degrees
    float userZoom = 1.0f;

    static constexpr float ZOOM_EASE = 0.08f;
    static constexpr float FOLLOW_EASE = 0.18f;
    static constexpr float ROLL_EASE = 0.08f;
    // GROUND_ZOOM / FRAMING are live-tunable in gDebug (cameraGroundZoom / cameraFraming).

    // Manual zoom layered on top of the automatic framing (scroll wheel / keys).
    static constexpr float USER_ZOOM_MIN = 0.25f;
    static constexpr float USER_ZOOM_MAX = 4.0f;
    static constexpr float USER_ZOOM_SENSITIVITY = 0.0015f;  // per wheel delta unit
    static constexpr float USER_ZOOM_KEY_RATE = 0.04f;       // per frame, keys held

    // world -> clip (-1..1), +y down on screen (matches p5).
    Mat3 matrix(int w, int h) const {
        Mat3 proj = Mat3::translation(-1.0f, 1.0f) * Mat3::scaling(2.0f / w, -2.0f / h);
        Mat3 center = Mat3::translation(w * 0.5f, h * 0.5f);
        Mat3 rot = Mat3::rotation(rotation);
        Mat3 sc = Mat3::scaling(scale, scale);
        Mat3 foc = Mat3::translation(-focusX, -focusY);
        return proj * center * rot * sc * foc;
    }
};

enum GameState { PLAYING = 0, LANDED = 1, CRASHED = 2 };

struct App {
    Renderer2D renderer;
    Planet planet;
    Lander lander;
    View view;
    GameState state = PLAYING;

    std::vector<Cow> cows;
    int cargo = 0;
    int score = 0;

    std::vector<Vec2> trajectory;   // reused coast-path buffer

    struct Star { float x, y, size, brightness; };
    std::vector<Star> stars;

    // Fullscreen shader passes (atmosphere + clouds).
    GLuint atmoProgram = 0;
    GLuint cloudProgram = 0;
    GLuint terminatorProgram = 0;   // day/night darkening over the planet body
    GLuint fsVao = 0, fsVbo = 0;
    GLuint terrainTex = 0;          // 2048x1 R32F: terrain radius per angle (the silhouette)

    // Previous-frame key states for edge-triggered actions (lift off / restart).
    bool prevThrustKey = false;
    bool prevRestartKey = false;

    double startTime = 0;
    double lastFrameTime = 0;
    int width = 800;
    int height = 600;
};

static App app;

static constexpr int KEY_R = 82;
static constexpr float LANDING_MAX_SLOPE = 40.0f; // degrees off the local tangent
static constexpr int MAX_CARGO = 3;
static constexpr int NUM_COWS = 5;

// Barycentric point-in-triangle test (ported from lander.js).
static bool pointInTriangle(Vec2 p, Vec2 a, Vec2 b, Vec2 c) {
    float area = 0.5f * (-b.y * c.x + a.y * (-b.x + c.x) + a.x * (b.y - c.y) + b.x * c.y);
    if (area == 0) return false;
    float s = (1.0f / (2.0f * area)) *
              (a.y * c.x - a.x * c.y + (c.y - a.y) * p.x + (a.x - c.x) * p.y);
    float t = (1.0f / (2.0f * area)) *
              (a.x * b.y - a.y * b.x + (a.y - b.y) * p.x + (b.x - a.x) * p.y);
    return s > 0 && t > 0 && s + t < 1;
}

// ============================================================================
static void spawnCows() {
    app.cows.clear();
    unsigned int s = 24601;
    auto rnd = [&s]() { s = s * 1664525u + 1013904223u; return (s >> 8) * (1.0f / 16777216.0f); };
    // Cluster the herd tightly around the ship's drop-in angle (-90). The planet
    // is huge (~32000 unit radius), so even half a degree is several hundred
    // units of arc — keep the spread small so you arrive right among them.
    for (int i = 0; i < NUM_COWS; i++) {
        // First cow sits right at the drop angle (you land next to it); the rest
        // spread slightly to either side.
        float angle = (i == 0) ? -90.0f : -90.0f + (rnd() * 1.0f - 0.5f);
        float r = app.planet.surfaceRadius(angle) + 25.0f;
        Cow cow;
        cow.spawn(app.planet, angle, r);
        app.cows.push_back(cow);
    }
}

static void resetGame() {
    app.lander.reset();
    app.lander.spawnOnPlanet(app.planet);
    app.state = PLAYING;
    app.cargo = 0;
    app.score = 0;
    spawnCows();

    app.view.focusX = app.lander.pos.x;
    app.view.focusY = app.lander.pos.y;
    app.view.scale = gDebug.cameraGroundZoom;

    // Align the initial roll so gravity points screen-down.
    Vec2 g = app.lander.gravityAt(app.planet, app.lander.pos);
    app.view.rotation = 90.0f - atan2d(g.y, g.x);
}

// Local terrain slope test (ports checkSafeLanding's flatness check): compare
// the surface segment around `angle` against the pure circular tangent.
static bool terrainFlat(const Planet& p, float angle) {
    float a1 = angle - 0.5f, a2 = angle + 0.5f;
    float r1 = p.surfaceRadius(a1), r2 = p.surfaceRadius(a2);
    Vec2 p1(p.center.x + r1 * cosd(a1), p.center.y + r1 * sind(a1));
    Vec2 p2(p.center.x + r2 * cosd(a2), p.center.y + r2 * sind(a2));
    Vec2 s = (p2 - p1).normalize();
    float tx = -sind(angle), ty = cosd(angle);   // tangent (perp to radial)
    float alignment = fabsf(s.x * tx + s.y * ty);
    return acosd(alignment) < LANDING_MAX_SLOPE;
}

// Track altitude every frame; while PLAYING, judge ground contact as a safe
// landing (slow, upright, flat) or a crash.
static void updateContact() {
    Lander& l = app.lander;
    Planet& p = app.planet;
    float dx = l.pos.x - p.center.x;
    float dy = l.pos.y - p.center.y;
    float d = sqrtf(dx * dx + dy * dy);
    float angle = atan2d(dy, dx);
    float alt = d - p.surfaceRadius(angle);
    l.altitude = alt > 0 ? alt : 0;

    if (!l.active) return;
    if (alt <= l.radius) {
        bool speedSafe = l.vel.length() < Lander::LANDING_MAX_SPEED;
        bool upright = l.isUpright(p);
        bool flat = terrainFlat(p, angle);
        if (speedSafe && upright && flat) {
            l.land(p);
            app.state = LANDED;
        } else {
            l.crash();
            app.state = CRASHED;
        }
    }
}

// Fullscreen quad covering clip space, shared by both shader passes.
static void initShaderPasses() {
    app.atmoProgram = Shader::createProgram(kFsVert, kAtmoFrag);
    app.cloudProgram = Shader::createProgram(kFsVert, kCloudFrag);
    app.terminatorProgram = Shader::createProgram(kFsVert, kTerminatorFrag);

    float quad[] = { -1, -1,  1, -1,  -1, 1,  -1, 1,  1, -1,  1, 1 };
    glGenVertexArrays(1, &app.fsVao);
    glGenBuffers(1, &app.fsVbo);
    glBindVertexArray(app.fsVao);
    glBindBuffer(GL_ARRAY_BUFFER, app.fsVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    // 2048x1 float texture holding the terrain radius per angle (the silhouette),
    // so the atmosphere shader knows exactly where the ground meets the sky.
    // High resolution (>> the ~255-vertex terrain polygon) so the sampled
    // silhouette faithfully follows the real peaks instead of aliasing against
    // them and cutting the tips short (which left an undarkened grey rim).
    glGenTextures(1, &app.terrainTex);
    glBindTexture(GL_TEXTURE_2D, app.terrainTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, 2048, 1, 0, GL_RED, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

// Resample the planet silhouette onto a uniform 2048-angle grid and upload it to
// the terrain texture. Call after the planet's terrain changes.
static void uploadTerrainLUT() {
    const int N = 2048;
    float lut[N];
    for (int k = 0; k < N; k++) {
        lut[k] = app.planet.surfaceRadius(k * (360.0f / N));
    }
    glBindTexture(GL_TEXTURE_2D, app.terrainTex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, N, 1, GL_RED, GL_FLOAT, lut);
}

// Shared uniform setup: camera reconstruction params common to both passes.
static void setPassViewUniforms(GLuint pr) {
    glUniform2f(glGetUniformLocation(pr, "u_resolution"), (float)app.width, (float)app.height);
    glUniform2f(glGetUniformLocation(pr, "u_viewFocus"), app.view.focusX, app.view.focusY);
    glUniform2f(glGetUniformLocation(pr, "u_screenCenter"), app.width * 0.5f, app.height * 0.5f);
    glUniform1f(glGetUniformLocation(pr, "u_viewScale"), app.view.scale);
    glUniform1f(glGetUniformLocation(pr, "u_viewRotation"), app.view.rotation * DEG2RAD);
}

// World position of the sun, sitting on the planet's sunward atmosphere edge
// and drifting with the day cycle. This single point is the light source for
// the atmosphere + clouds AND where the visible sun is drawn, so they all agree.
static Vec2 sunWorldPos(float t) {
    const Planet& p = app.planet;
    float ang = t * 0.005f;
    Vec2 dir(sinf(ang), -cosf(ang));             // base (0,-1) rotated by the day cycle
    float R = p.baseRadius + 0.55f * (p.atmosphereOuterRadius() - p.baseRadius);
    return Vec2(p.center.x + dir.x * R, p.center.y + dir.y * R);
}

// How strongly the sky + clouds fill the planet disk, as a function of the
// ship's altitude: 0 near the surface (so the ground stays clear in front of
// you) ramping to gDebug.spaceHaze once you're clearly out in space. This makes
// the atmosphere read as a full sphere with the planet showing through.
static float spaceOverlay() {
    const Planet& p = app.planet;
    float dx = app.lander.pos.x - p.center.x;
    float dy = app.lander.pos.y - p.center.y;
    float distC = sqrtf(dx * dx + dy * dy);
    float thickness = p.atmosphereOuterRadius() - p.baseRadius;
    float altFrac = (distC - p.baseRadius) / (thickness > 1.0f ? thickness : 1.0f);
    // Crossfade across the upper atmosphere so the sky is a complete circle right
    // as you exit at the atmosphere top (altFrac ~ 1.0).
    float ramp = altFrac <= 0.55f ? 0.0f
               : altFrac >= 1.0f  ? 1.0f
               : (altFrac - 0.55f) / (1.0f - 0.55f);
    return ramp * gDebug.spaceHaze;
}

static void drawCloudsPass(float t) {
    const int N = SHADER_MAX_PLANETS;
    float pos[N * 2] = {0}, outerR[N] = {0};
    const Planet& p = app.planet;
    pos[0] = p.center.x; pos[1] = p.center.y;
    float mountainTop = p.maxSurfaceRadius();
    outerR[0] = mountainTop + (p.atmosphereOuterRadius() - mountainTop) * gDebug.cloudHeight;

    GLuint pr = app.cloudProgram;
    glUseProgram(pr);
    setPassViewUniforms(pr);
    glUniform1f(glGetUniformLocation(pr, "u_time"), t);
    glUniform1i(glGetUniformLocation(pr, "u_planetCount"), 1);
    glUniform2fv(glGetUniformLocation(pr, "u_planetPos"), N, pos);
    glUniform1fv(glGetUniformLocation(pr, "u_planetOuterR"), N, outerR);
    glUniform1f(glGetUniformLocation(pr, "u_coverage"), gDebug.cloudCoverage);
    glUniform1f(glGetUniformLocation(pr, "u_falloff"), gDebug.cloudFalloff);
    glUniform1f(glGetUniformLocation(pr, "u_overlay"), spaceOverlay());
    glUniform1f(glGetUniformLocation(pr, "u_nightDarkness"), gDebug.nightDarkness);
    glUniform1f(glGetUniformLocation(pr, "u_softness"), gDebug.nightSoftness);
    Vec2 sunW = sunWorldPos(t);
    glUniform2f(glGetUniformLocation(pr, "u_sunPos"), sunW.x, sunW.y);  // sun on the atmosphere edge
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, app.terrainTex);
    glUniform1i(glGetUniformLocation(pr, "u_terrainTex"), 0);

    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);   // premultiplied "over"
    glBindVertexArray(app.fsVao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

// Darkens the planet body on the side facing away from the sun (day/night
// terminator). Drawn right after the planet body as a multiply pass so the night
// side actually goes dark; the atmosphere pass then adds its faint sky glow on
// top. Runs before the cows so abductable cows stay readable even at night.
static void drawTerminatorPass(float t) {
    const int N = SHADER_MAX_PLANETS;
    float pos[N * 2] = {0}, maxR[N] = {0};
    const Planet& p = app.planet;
    pos[0] = p.center.x; pos[1] = p.center.y;
    // Bound the multiply pass by the generated silhouette. Mountain ranges can
    // exceed the raw noise intensity, so an estimate can leave tall night-side
    // peaks undarkened.
    maxR[0] = p.maxSurfaceRadius() + 2.0f;

    GLuint pr = app.terminatorProgram;
    glUseProgram(pr);
    setPassViewUniforms(pr);
    glUniform1i(glGetUniformLocation(pr, "u_planetCount"), 1);
    glUniform2fv(glGetUniformLocation(pr, "u_planetPos"), N, pos);
    glUniform1fv(glGetUniformLocation(pr, "u_planetMaxR"), N, maxR);
    glUniform1f(glGetUniformLocation(pr, "u_nightDarkness"), gDebug.nightDarkness);
    glUniform1f(glGetUniformLocation(pr, "u_softness"), gDebug.nightSoftness);
    Vec2 sunW = sunWorldPos(t);
    glUniform2f(glGetUniformLocation(pr, "u_sunPos"), sunW.x, sunW.y);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, app.terrainTex);
    glUniform1i(glGetUniformLocation(pr, "u_terrainTex"), 0);

    glEnable(GL_BLEND);
    glBlendFunc(GL_ZERO, GL_SRC_COLOR);   // framebuffer *= shade (multiply)
    glBindVertexArray(app.fsVao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);   // restore the default "over"
}

// One atmosphere disk (radius = atmosphere top), drawn once after the planet.
// The shader splits it by radius: above the mountain tops it's the scattering
// horizon sky / limb halo; over the planet face it's the clean, altitude-gated
// sky sphere. So the sky reads as a single circle with the planet underneath.
static void drawAtmospherePass(float t) {
    const int N = SHADER_MAX_PLANETS;
    float pos[N * 2] = {0}, prad[N] = {0}, arad[N] = {0};
    const Planet& p = app.planet;
    pos[0] = p.center.x; pos[1] = p.center.y;
    // baseR is the sky-gradient reference; atmoR is the atmosphere top. The actual
    // ground/sky boundary per angle comes from the terrain texture in the shader.
    prad[0] = p.baseRadius;
    arad[0] = p.atmosphereOuterRadius();

    GLuint pr = app.atmoProgram;
    glUseProgram(pr);
    setPassViewUniforms(pr);
    glUniform1f(glGetUniformLocation(pr, "u_time"), t);
    glUniform1i(glGetUniformLocation(pr, "u_planetCount"), 1);
    glUniform2fv(glGetUniformLocation(pr, "u_planetPos"), N, pos);
    glUniform1fv(glGetUniformLocation(pr, "u_planetRadius"), N, prad);
    glUniform1fv(glGetUniformLocation(pr, "u_atmoRadius"), N, arad);
    glUniform1f(glGetUniformLocation(pr, "u_rayleighH"), gDebug.atmRayleighH);
    glUniform1f(glGetUniformLocation(pr, "u_rimH"), gDebug.atmRimH);
    glUniform1f(glGetUniformLocation(pr, "u_mieH"), gDebug.atmMieH);
    glUniform1f(glGetUniformLocation(pr, "u_sunIntensity"), gDebug.atmSunIntensity);
    glUniform1f(glGetUniformLocation(pr, "u_extinction"), gDebug.atmExtinction);
    glUniform1f(glGetUniformLocation(pr, "u_overlay"), spaceOverlay());
    Vec2 sunW = sunWorldPos(t);
    glUniform2f(glGetUniformLocation(pr, "u_sunPos"), sunW.x, sunW.y);  // sun on the atmosphere edge
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, app.terrainTex);
    glUniform1i(glGetUniformLocation(pr, "u_terrainTex"), 0);

    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);   // premultiplied "over": emits + occludes stars
    glBindVertexArray(app.fsVao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

static void init() {
    app.renderer.init();
    initShaderPasses();

    // One starter planet, ported tuning: radiusScale 4.0 * 8000 base, density
    // 0.03, big noise span and dense sampling for a jagged silhouette.
    app.planet.generate(Vec2(0, 0), 8000.0f * 4.0f, 2000.0f, 255, gDebug.planetDensity, 1337);
    uploadTerrainLUT();   // seed the silhouette texture for the atmosphere shader

    // Static clip-space starfield (drawn behind everything).
    unsigned int s = 9871;
    auto rnd = [&s]() { s = s * 1664525u + 1013904223u; return (s >> 8) * (1.0f / 16777216.0f); };
    for (int i = 0; i < 320; i++) {
        app.stars.push_back({rnd() * 2.0f - 1.0f, rnd() * 2.0f - 1.0f,
                             0.0008f + rnd() * 0.0022f, 0.35f + rnd() * 0.65f});
    }

    Input::init("#canvas");
    resetGame();

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    app.startTime = emscripten_get_now() / 1000.0;
    app.lastFrameTime = app.startTime;

    printf("=== Cow Abductor (C++/WebGL) ===\n");
    printf("W/Up - thrust | A/D or Left/Right - rotate\n");
}

static void pollInput(float timeScale) {
    Lander& l = app.lander;

    bool left  = Input::isKeyDown(Input::KEY_A) || Input::isKeyDown(Input::KEY_LEFT);
    bool right = Input::isKeyDown(Input::KEY_D) || Input::isKeyDown(Input::KEY_RIGHT);
    if (left)  l.rotate(-0.2f * timeScale);
    if (right) l.rotate(0.2f * timeScale);

    bool thrust = Input::isKeyDown(Input::KEY_W) || Input::isKeyDown(Input::KEY_UP);
    l.setThrust(thrust && l.fuel > 0 ? 1.0f : 0.0f);

    // Tractor beam fires straight down the ship's local axis while space is held.
    l.abducting = Input::isKeyDown(Input::KEY_SPACE);
}

// Decide each frame whether each loose cow is inside the active beam cone and
// pull it in (ported from lander.js abduct()). Returns nothing; cows transition
// their own state and report stowing back through update().
static void runBeamGrab() {
    Lander& l = app.lander;
    bool canGrab = l.beamOn && app.cargo < MAX_CARGO;
    for (Cow& cow : app.cows) {
        if (cow.state == Cow::State::Stowed) continue;
        bool inBeam = false;
        if (canGrab) {
            bool close = dist(l.pos.x, l.pos.y, cow.pos.x, cow.pos.y) < 50.0f;
            inBeam = pointInTriangle(cow.pos, l.beamA, l.beamB, l.beamC) || close;
        }
        if (inBeam) cow.abduct();
        else cow.drop();
    }
}

static void updateView(float timeScale) {
    Lander& l = app.lander;
    View& v = app.view;

    // Altitude-based framing, split at the atmosphere boundary (see updateView
    // in sketch.js). surfaceDistance is measured from the spherical base radius.
    float d = dist(l.pos.x, l.pos.y, app.planet.center.x, app.planet.center.y);
    float surfaceDistance = d - app.planet.baseRadius;
    float atmoThickness = app.planet.atmosphereOuterRadius() - app.planet.baseRadius;
    if (atmoThickness < 1) atmoThickness = 1;
    float boundaryScale = (app.height * gDebug.cameraFraming) / atmoThickness;

    float targetScale;
    if (surfaceDistance <= atmoThickness) {
        float f = surfaceDistance / atmoThickness;       // 1 at top, 0 at surface
        if (f < 0) f = 0;
        float gz = gDebug.cameraGroundZoom;
        targetScale = gz + (boundaryScale - gz) * f;
    } else {
        targetScale = (app.height * gDebug.cameraFraming) / surfaceDistance;
    }
    if (targetScale < 0.02f) targetScale = 0.02f;
    targetScale *= v.userZoom;

    v.scale += (targetScale - v.scale) * View::ZOOM_EASE * timeScale;

    // Velocity-predictive follow so easing only corrects displacement.
    v.focusX += l.vel.x * timeScale + (l.pos.x - v.focusX) * View::FOLLOW_EASE * timeScale;
    v.focusY += l.vel.y * timeScale + (l.pos.y - v.focusY) * View::FOLLOW_EASE * timeScale;

    // Roll so net gravity points screen-down.
    Vec2 g = l.gravityAt(app.planet, l.pos);
    if (g.lengthSq() > 1e-12f) {
        float target = 90.0f - atan2d(g.y, g.x);
        float delta = std::fmod(target - v.rotation + 540.0f, 360.0f) - 180.0f;
        v.rotation += delta * View::ROLL_EASE * timeScale;
    }
}

// Manual zoom: scroll wheel (exponential, constant ratio per notch) plus
// +/- keys for held continuous zoom. Layered on the automatic framing via
// view.userZoom, clamped so the ship can't be lost off-screen.
static void updateZoom(float timeScale) {
    View& v = app.view;
    float wd = Input::getWheelDelta();
    if (wd != 0.0f) {
        v.userZoom *= expf(-wd * View::USER_ZOOM_SENSITIVITY);
        Input::resetWheelDelta();
    }
    float k = 0.0f;
    if (Input::isKeyDown(187) || Input::isKeyDown(107)) k += 1.0f; // '=' / numpad +
    if (Input::isKeyDown(189) || Input::isKeyDown(109)) k -= 1.0f; // '-' / numpad -
    if (k != 0.0f) v.userZoom *= expf(k * View::USER_ZOOM_KEY_RATE * timeScale);

    if (v.userZoom < View::USER_ZOOM_MIN) v.userZoom = View::USER_ZOOM_MIN;
    if (v.userZoom > View::USER_ZOOM_MAX) v.userZoom = View::USER_ZOOM_MAX;
}

// Predicted coast path, drawn as a fading line that thins/fades along its
// length. Width is kept ~constant on screen by dividing by the view scale.
static void drawTrajectory() {
    Renderer2D& r = app.renderer;
    app.lander.predictTrajectory(app.planet, app.trajectory);
    const auto& pts = app.trajectory;
    int n = (int)pts.size();
    if (n < 2) return;
    float w = 1.6f / app.view.scale;
    for (int i = 0; i + 1 < n; i++) {
        // Gentle fade so the far end of the arc stays visible (brightest at ship).
        float fade = 1.0f - 0.6f * ((float)i / n);
        r.fill(0.72f, 0.85f, 1.0f, 0.5f * fade);
        r.line(pts[i].x, pts[i].y, pts[i + 1].x, pts[i + 1].y, w);
    }
}

// Visible sun, anchored to the planet's sunward atmosphere limb (the brightest
// point of the Rayleigh band), tracking the same day-cycle drift as the sky
// shader — ported from the original's flareSourceWorld()/drawSunFlare(). A
// stacked additive glow + bright core, sized in screen pixels so it reads as a
// constant sun whether you're on the surface or out in space.
static void drawSun(float t, const Mat3& viewMat) {
    Vec2 pos = sunWorldPos(t);                     // same point that lights the sky
    float s = 1.0f / app.view.scale;               // world units per screen pixel
    Renderer2D& r = app.renderer;
    r.begin(viewMat, app.width, app.height);
    r.setBlend(Renderer2D::Blend::Additive);
    // Soft halo: stacked discs from a wide faint glow down to a tight bright one.
    for (int i = 0; i < 6; i++) {
        float tt = i / 5.0f;
        float rad = (130.0f + (14.0f - 130.0f) * tt) * s;
        r.fill(1.0f, 0.94f, 0.80f, 0.04f + 0.20f * tt);
        r.circle(pos.x, pos.y, rad, 28);
    }
    // Bright core.
    r.fill(1.0f, 0.98f, 0.88f, 0.95f);
    r.circle(pos.x, pos.y, 15.0f * s, 22);
    r.setBlend(Renderer2D::Blend::Normal);
    r.end();
}

// Far-view stand-in for the ship: a bright, blinking bluish-green beacon echoing
// the hull's glowing cockpit/core, so the ship is findable when its sprite would
// be a sub-pixel speck. Screen-constant size; additive layers for the glow.
static void drawShipBeacon() {
    Renderer2D& r = app.renderer;
    float s = 1.0f / app.view.scale;                 // world units per screen px
    float blink = 0.45f + 0.55f * sind(app.lander.anim * 8.0f);  // ~0.75s cycle
    Vec2 p = app.lander.pos;

    r.setBlend(Renderer2D::Blend::Additive);
    r.fill(0.30f, 1.0f, 0.78f, 0.22f * blink + 0.05f);
    r.circle(p.x, p.y, 17.0f * s, 22);
    r.fill(0.40f, 1.0f, 0.86f, 0.40f * blink + 0.10f);
    r.circle(p.x, p.y, 9.0f * s, 18);
    r.fill(0.82f, 1.0f, 0.95f, 0.55f + 0.45f * blink);
    r.circle(p.x, p.y, 4.5f * s, 14);
    r.setBlend(Renderer2D::Blend::Normal);
}

// Red heading arrow at the ship, pointing along the nose. Sized in screen
// pixels (÷ view.scale) so it stays readable when the ship is a distant speck.
static void drawHeadingArrow() {
    Renderer2D& r = app.renderer;
    Vec2 nose = app.lander.noseDir();
    Vec2 perp(-nose.y, nose.x);
    float s = 1.0f / app.view.scale;          // world units per screen pixel
    Vec2 base = app.lander.pos + nose * (12.0f * s);
    Vec2 shaftEnd = base + nose * (26.0f * s);
    Vec2 tip = shaftEnd + nose * (16.0f * s);
    Vec2 hl = shaftEnd + perp * (11.0f * s);
    Vec2 hr = shaftEnd - perp * (11.0f * s);

    r.fill(1.0f, 0.22f, 0.16f, 0.95f);
    r.line(base.x, base.y, shaftEnd.x, shaftEnd.y, 4.0f * s);
    r.triangle(tip.x, tip.y, hl.x, hl.y, hr.x, hr.y);
}

static void drawStars() {
    Renderer2D& r = app.renderer;
    r.beginScreenSpace();
    float aspect = (float)app.width / (float)app.height;
    for (const auto& st : app.stars) {
        r.fill(st.brightness, st.brightness, st.brightness * 1.05f, 1.0f);
        // Square quad; correct for aspect so stars stay round-ish.
        float hx = st.size / aspect;
        float hy = st.size;
        r.quad(st.x - hx, st.y - hy, st.x + hx, st.y - hy,
               st.x + hx, st.y + hy, st.x - hx, st.y + hy);
    }
    r.endScreenSpace();
}

// Pull the live tunables from the HTML debug panel (window.DBG) into gDebug.
// Most are read straight through; planet density is applied via setDensity only
// when it changes, and max-fuel clamps the current tank if lowered.
static void syncDebugFromJS() {
    gDebug.thrust           = EM_ASM_DOUBLE({ return window.DBG.thrust; });
    gDebug.maxFuel          = EM_ASM_DOUBLE({ return window.DBG.maxFuel; });
    gDebug.beamRange        = EM_ASM_DOUBLE({ return window.DBG.beamRange; });
    gDebug.beamWidth        = EM_ASM_DOUBLE({ return window.DBG.beamWidth; });
    gDebug.atmosphereScale  = EM_ASM_DOUBLE({ return window.DBG.atmosphereScale; });
    gDebug.atmosphereDrag   = EM_ASM_DOUBLE({ return window.DBG.atmosphereDrag; });
    gDebug.atmRayleighH     = EM_ASM_DOUBLE({ return window.DBG.atmRayleighH; });
    gDebug.atmRimH          = EM_ASM_DOUBLE({ return window.DBG.atmRimH; });
    gDebug.atmMieH          = EM_ASM_DOUBLE({ return window.DBG.atmMieH; });
    gDebug.atmSunIntensity  = EM_ASM_DOUBLE({ return window.DBG.atmSunIntensity; });
    gDebug.atmExtinction    = EM_ASM_DOUBLE({ return window.DBG.atmExtinction; });
    gDebug.cloudHeight      = EM_ASM_DOUBLE({ return window.DBG.cloudHeight; });
    gDebug.cloudCoverage    = EM_ASM_DOUBLE({ return window.DBG.cloudCoverage; });
    gDebug.cloudFalloff     = EM_ASM_DOUBLE({ return window.DBG.cloudFalloff; });
    gDebug.spaceHaze        = EM_ASM_DOUBLE({ return window.DBG.spaceHaze; });
    gDebug.nightDarkness    = EM_ASM_DOUBLE({ return window.DBG.nightDarkness; });
    gDebug.nightSoftness    = EM_ASM_DOUBLE({ return window.DBG.nightSoftness; });
    gDebug.cameraGroundZoom = EM_ASM_DOUBLE({ return window.DBG.cameraGroundZoom; });
    gDebug.cameraFraming    = EM_ASM_DOUBLE({ return window.DBG.cameraFraming; });

    float dens = EM_ASM_DOUBLE({ return window.DBG.planetDensity; });
    if (dens != gDebug.planetDensity) {
        gDebug.planetDensity = dens;
        app.planet.setDensity(dens);
    }
    if (app.lander.fuel > gDebug.maxFuel) app.lander.fuel = gDebug.maxFuel;
}

static void mainLoop() {
    // Resize handling.
    int w, h;
    emscripten_get_canvas_element_size("#canvas", &w, &h);
    if (w != app.width || h != app.height) {
        app.width = w;
        app.height = h;
        glViewport(0, 0, w, h);
    }

    double now = emscripten_get_now() / 1000.0;
    float dt = (float)(now - app.lastFrameTime);
    app.lastFrameTime = now;
    if (dt > 0.05f) dt = 0.05f;        // clamp to avoid huge catch-up steps
    float timeScale = dt * 60.0f;       // 1 == one 60fps frame (original tuning)

    syncDebugFromJS();

    // Edge-triggered keys: thrust (lift off from a landing) and R (restart).
    bool thrustKey = Input::isKeyDown(Input::KEY_W) || Input::isKeyDown(Input::KEY_UP);
    bool restartKey = Input::isKeyDown(KEY_R);
    bool thrustPressed = thrustKey && !app.prevThrustKey;
    bool restartPressed = restartKey && !app.prevRestartKey;
    app.prevThrustKey = thrustKey;
    app.prevRestartKey = restartKey;

    switch (app.state) {
        case PLAYING:
            pollInput(timeScale);
            app.lander.update(app.planet, timeScale);
            updateContact();
            app.lander.updateBeam(app.planet);
            runBeamGrab();
            for (Cow& cow : app.cows) {
                if (cow.update(app.planet, app.lander, app.cargo, MAX_CARGO, timeScale)) {
                    app.cargo += 1;
                    app.score += 50;
                }
            }
            break;
        case LANDED:
            // Stick to the body (no-op while it's stationary, but ready for orbits).
            app.lander.pos = app.planet.center + app.lander.landingOffset;
            updateContact();                 // keep altitude readout fresh
            if (thrustPressed) {
                app.lander.liftOff(app.planet);
                app.state = PLAYING;
            }
            break;
        case CRASHED:
            if (restartPressed) resetGame();
            break;
    }

    updateZoom(timeScale);
    updateView(timeScale);

    float shaderTime = (float)(now - app.startTime) * 3.0f;  // ~= original frameCount*0.05

    // Trajectory + heading arrow are space aids: only shown once the hull has
    // shrunk below ~legibility on screen, so they don't clutter the close view.
    bool flying = (app.state == PLAYING && app.lander.active);
    bool shipIsSmall = (24.0f * app.view.scale) < 22.0f;  // hull half-height in px
    bool spaceAids = flying && shipIsSmall;
    // The heading arrow only matters while maneuvering, so show it only while a
    // thrust/rotate key is held; the beacon + trajectory stay up regardless.
    bool steering = Input::isKeyDown(Input::KEY_W) || Input::isKeyDown(Input::KEY_UP) ||
                    Input::isKeyDown(Input::KEY_A) || Input::isKeyDown(Input::KEY_LEFT) ||
                    Input::isKeyDown(Input::KEY_D) || Input::isKeyDown(Input::KEY_RIGHT);

    glClear(GL_COLOR_BUFFER_BIT);
    Mat3 viewMat = app.view.matrix(app.width, app.height);

    // 1) Deep background: starfield.
    app.renderer.begin(viewMat, app.width, app.height);
    drawStars();
    app.renderer.end();

    // 2) Solid planet + surface.
    app.renderer.begin(viewMat, app.width, app.height);
    app.planet.draw(app.renderer);
    app.renderer.end();

    // 2b) Day/night terminator: darken the planet body's far-from-sun hemisphere
    //     (multiply pass) before the cows go down, so cows stay readable at night.
    drawTerminatorPass(shaderTime);

    app.renderer.begin(viewMat, app.width, app.height);
    for (const Cow& cow : app.cows) cow.draw(app.renderer, app.planet);
    app.renderer.end();

    // 3) Atmosphere — one disk over the planet: horizon sky above the mountains,
    //    and (from space) the clean sky sphere over the planet face. The ground
    //    stays clear at low altitude (the face fill is gated by altitude).
    drawAtmospherePass(shaderTime);

    // 3c) Ship + space aids stay crisp on top of the sky.
    app.renderer.begin(viewMat, app.width, app.height);
    if (spaceAids) drawTrajectory();
    app.lander.drawBeam(app.renderer);
    app.lander.draw(app.renderer);   // always draw the actual ship
    if (spaceAids) {
        // In space the ship is sub-pixel, so overlay a locator beacon on top of it.
        drawShipBeacon();
        if (steering) drawHeadingArrow();
    }
    app.renderer.end();

    // 4) Clouds (alpha) in front of everything, incl. the ship (fly behind them).
    drawCloudsPass(shaderTime);

    // 5) The sun, glowing on the atmosphere's sunward limb (additive, on top).
    // drawSun(shaderTime, viewMat);

    // Push HUD state to the DOM overlay (defined in shell.html).
    EM_ASM({
        if (window.updateHUD) window.updateHUD($0, $1, $2, $3, $4, $5, $6, $7);
    }, app.lander.fuel, gDebug.maxFuel, app.lander.altitude,
       app.lander.vel.length(), (int)app.state, app.cargo, MAX_CARGO, app.score);
}

int main() {
    EmscriptenWebGLContextAttributes attrs;
    emscripten_webgl_init_context_attributes(&attrs);
    attrs.majorVersion = 2;
    attrs.minorVersion = 0;
    attrs.alpha = false;
    attrs.depth = false;
    attrs.stencil = false;
    attrs.antialias = true;

    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE ctx = emscripten_webgl_create_context("#canvas", &attrs);
    if (ctx <= 0) {
        printf("Failed to create WebGL 2 context!\n");
        return 1;
    }
    emscripten_webgl_make_context_current(ctx);

    emscripten_get_canvas_element_size("#canvas", &app.width, &app.height);
    glViewport(0, 0, app.width, app.height);

    init();
    emscripten_set_main_loop(mainLoop, 0, 1);
    return 0;
}
