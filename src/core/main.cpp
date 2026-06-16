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
#include "../entities/astronaut.h"
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
uniform int u_mode;             // 0 = sky band above the terrain, 1 = face fill below it
uniform sampler2D u_terrainTex; // 2048xN: terrain radius per angle (one row per planet)
out vec4 fragColor;

#define TERRAIN_N 2048

// Terrain (surface) radius at a given outward direction, sampled from the
// silhouette LUT with linear wrap so the sky meets the ground exactly.
float terrainRadius(vec2 outward, int row) {
  float ang = degrees(atan(outward.y, outward.x));
  if (ang < 0.0) ang += 360.0;
  float fidx = ang / 360.0 * float(TERRAIN_N);
  int i0 = int(fidx) % TERRAIN_N;
  int i1 = (i0 + 1) % TERRAIN_N;
  float frac = fract(fidx);
  return mix(texelFetch(u_terrainTex, ivec2(i0, row), 0).r,
             texelFetch(u_terrainTex, ivec2(i1, row), 0).r, frac);
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
    // The pass runs twice per frame: mode 0 draws only the open-sky band (so
    // the ridge layers can be painted over it and the terrain over those), and
    // mode 1 draws only the face fill (after the terrain, so the from-space
    // sky sphere still tints the rock).
    float surfaceR = terrainRadius(outward, i);
    bool belowSurface = dist < surfaceR;
    if (u_mode == 0 && belowSurface) continue;
    if (u_mode == 1 && !belowSurface) continue;

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
float terrainRadius(vec2 outward, int row) {
  float ang = degrees(atan(outward.y, outward.x));
  if (ang < 0.0) ang += 360.0;
  float fidx = ang / 360.0 * float(TERRAIN_N);
  int i0 = int(fidx) % TERRAIN_N;
  int i1 = (i0 + 1) % TERRAIN_N;
  float frac = fract(fidx);
  return mix(texelFetch(u_terrainTex, ivec2(i0, row), 0).r,
             texelFetch(u_terrainTex, ivec2(i1, row), 0).r, frac);
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
    float surfaceR = terrainRadius(outwardDir, i);
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

// Planet body pass. Replaces the old marching-squares mesh + separate
// terminator multiply with ONE fullscreen pass that samples the destructible
// density field (TerrainField's corner grid, mirrored into an R16F texture) per
// pixel. The 0 iso-line is the rock surface, anti-aliased against the pixel
// footprint so edges stay smooth at any zoom. Far planets (without a live
// field) render through the same path from their silhouette LUT rows. On top
// of the fill it computes, per pixel:
//   - material: altitude-banded color (dark basins -> rock -> ragged snow caps)
//     with FBM grain, the live-shader port of the original's baked planet
//     texture (planet.js generateSurfaceTexture);
//   - slope lighting from the SDF gradient (the true 2D surface normal, so it
//     also lights the walls of dug craters and tunnels);
//   - the day/night terminator (formerly its own multiply pass, merged here);
//   - cave interior: open cells BELOW the silhouette render as near-black rock
//     backdrop instead of letting stars shine through the planet.
static const char* kBodyFrag = R"(#version 300 es
precision highp float;
#define MAX_PLANETS 10
uniform vec2 u_resolution;
uniform vec2 u_viewFocus;
uniform vec2 u_screenCenter;
uniform float u_viewScale;
uniform float u_viewRotation;
uniform int u_planetCount;
uniform vec2 u_planetPos[MAX_PLANETS];
uniform float u_planetMaxR[MAX_PLANETS];   // silhouette bound (-1e9 = skip, e.g. the sun)
uniform float u_baseRadius[MAX_PLANETS];   // altitude-band reference
uniform float u_noiseBand[MAX_PLANETS];    // terrain noise intensity (band thickness)
uniform int u_activeField;      // planet index using the density texture (-1 = none)
uniform vec2 u_sunPos;
uniform float u_nightDarkness;  // brightness floor of the deepest night side
uniform float u_softness;       // terminator half-width in sunDot units
uniform sampler2D u_terrainTex; // 2048x1: terrain radius per angle (the silhouette)
uniform sampler2D u_fieldTex;   // R16F corner density grid (LINEAR-filtered)
uniform vec2 u_fieldOrigin;     // world position of corner (0,0)
uniform float u_fieldCell;      // world units per grid cell
uniform vec2 u_fieldSize;       // corner counts (w,h)
out vec4 fragColor;

#define TERRAIN_N 2048
float terrainRadius(vec2 outward, int row) {
  float ang = degrees(atan(outward.y, outward.x));
  if (ang < 0.0) ang += 360.0;
  float fidx = ang / 360.0 * float(TERRAIN_N);
  int i0 = int(fidx) % TERRAIN_N;
  int i1 = (i0 + 1) % TERRAIN_N;
  float frac = fract(fidx);
  return mix(texelFetch(u_terrainTex, ivec2(i0, row), 0).r,
             texelFetch(u_terrainTex, ivec2(i1, row), 0).r, frac);
}

// Signed density at a world point: > 0 inside rock, < 0 in open space, 0 at the
// surface. CLAMP_TO_EDGE returns the (very negative) border corners outside the
// grid, so out-of-range reads correctly as open space.
float fieldDensity(vec2 world) {
  vec2 uv = ((world - u_fieldOrigin) / u_fieldCell + 0.5) / u_fieldSize;
  return texture(u_fieldTex, uv).r;
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

// Density for planet i at a world point: the live grid for the active-field
// planet, a radial pseudo-SDF from the silhouette row for everyone else. Both
// have ~unit gradient, so one material/lighting path serves near and far
// planets alike (far ones just can't have caves).
float planetDensity(int i, vec2 wpt) {
  if (i == u_activeField) return fieldDensity(wpt);
  vec2 dd = wpt - u_planetPos[i];
  float dl = length(dd);
  return terrainRadius(dd / max(dl, 0.0001), i) - dl;
}


void main() {
  // Reconstruct the world point under this pixel (inverse camera transform).
  vec2 pixel = vec2(gl_FragCoord.x, u_resolution.y - gl_FragCoord.y);
  vec2 centered = pixel - u_screenCenter;
  float c = cos(-u_viewRotation), s = sin(-u_viewRotation);
  vec2 unrot = vec2(c * centered.x - s * centered.y, s * centered.x + c * centered.y);
  vec2 world = unrot / u_viewScale + u_viewFocus;

  // Edge anti-aliasing width: both density sources are ~unit-gradient signed
  // distances, so one screen pixel spans ~1/viewScale density units. Analytic
  // (not fwidth) so it stays well-defined inside the divergent planet loop.
  float aaw = max(1.5 / u_viewScale, 0.0001);

  vec3 accumCol = vec3(0.0);
  float accumA = 0.0;
  for (int i = 0; i < MAX_PLANETS; i++) {
    if (i >= u_planetCount) break;
    vec2 d = world - u_planetPos[i];
    float dist = length(d);
    if (dist > u_planetMaxR[i] + u_fieldCell * 2.0) continue;
    vec2 outward = d / max(dist, 0.0001);
    float surfaceR = terrainRadius(outward, i);
    float density = planetDensity(i, world);

    // Rock coverage: anti-aliased step across the 0 iso-line.
    float rock = clamp(density / aaw + 0.5, 0.0, 1.0);
    // Cave interior: open space that sits below the outer silhouette (a dug
    // tunnel, or a sealed pocket). Rendered opaque so stars never show through.
    float below = 1.0 - smoothstep(surfaceR - u_fieldCell, surfaceR, dist);
    float cave = below * (1.0 - rock);
    if (rock <= 0.0 && cave <= 0.0) continue;

    // Day/night terminator (directional sun).
    vec2 toSun = normalize(u_sunPos - u_planetPos[i]);
    float sunDot = dot(outward, toSun);
    float shade = mix(u_nightDarkness, 1.0, smoothstep(-u_softness, u_softness, sunDot));

    // Surface normal from the SDF gradient: the density gradient points into
    // the rock, so its negation is the outward normal — valid on mountain
    // flanks AND inside dug pits. Faded out away from surfaces (large density)
    // so deep rock doesn't pick up radial striping.
    float e = u_fieldCell * 0.5;
    float gx = planetDensity(i, world + vec2(e, 0.0)) - planetDensity(i, world - vec2(e, 0.0));
    float gy = planetDensity(i, world + vec2(0.0, e)) - planetDensity(i, world - vec2(0.0, e));
    vec2 g = vec2(gx, gy);
    float slopeLight = 0.0;   // -1 shadow flank .. +1 sun-facing flank
    if (dot(g, g) > 1e-6) {
      vec2 N = -normalize(g);
      float skin = 1.0 - smoothstep(0.0, u_fieldCell * 6.0, density);
      slopeLight = dot(N, toSun) * skin;
    }

    // ---- Minimal banded material (see docs/art-style.md) -------------------
    // Five flat terrain tones picked by altitude, band boundaries jittered by
    // low-frequency FBM so they stay ragged organic shorelines instead of
    // concentric rings. No per-pixel grain: flat color fields ARE the style.
    vec2 local = d;
    float n = fbm(local * 0.00005);
    float altFrac = clamp((dist - u_baseRadius[i]) / max(u_noiseBand[i], 1.0), 0.0, 1.0);
    float ragged = altFrac + (n - 0.5) * 0.30;
    const vec3 T_BASIN = vec3(0.290, 0.271, 0.251);
    const vec3 T_MID   = vec3(0.431, 0.412, 0.392);
    const vec3 T_HIGH  = vec3(0.561, 0.533, 0.494);
    const vec3 T_CREST = vec3(0.690, 0.659, 0.604);
    const vec3 T_SNOW  = vec3(0.930, 0.950, 0.980);
    vec3 col = mix(T_BASIN, T_MID, smoothstep(0.16, 0.20, ragged));
    col = mix(col, T_HIGH,  smoothstep(0.42, 0.46, ragged));
    col = mix(col, T_CREST, smoothstep(0.60, 0.64, ragged));
    col = mix(col, T_SNOW,  smoothstep(0.76, 0.80, ragged));

    // Cel lighting: three flat light levels (night / shadow / day) with thin
    // anti-aliased boundaries. The terminator sweeps the planet as a clean
    // banded edge, and shadow-side mountain flanks drop one whole step.
    float lightT = shade * (0.80 + 0.20 * slopeLight);
    float q1 = smoothstep(0.34, 0.40, lightT);
    float q2 = smoothstep(0.66, 0.72, lightT);
    col *= 0.30 + 0.32 * q1 + 0.38 * q2;

    // Cave interior: near-black rock backdrop, darker with depth below the
    // silhouette so shafts read recessed.
    vec3 caveCol = vec3(0.055, 0.050, 0.045) *
                   mix(1.0, 0.35, clamp((surfaceR - dist) / max(u_noiseBand[i] * 2.0, 1.0), 0.0, 1.0)) *
                   shade;

    float alpha = clamp(rock + cave, 0.0, 1.0);
    vec3 rgb = col * rock + caveCol * cave;
    accumCol += rgb * (1.0 - accumA);
    accumA += alpha * (1.0 - accumA);
  }
  fragColor = vec4(accumCol, accumA);   // premultiplied "over"
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
    // The floor is deep so you can pull out far enough to see neighboring
    // bodies from the surface — this is an exploration game now.
    static constexpr float USER_ZOOM_MIN = 0.05f;
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
    // The solar system. planets[sunIdx] is the star; planets[homeIdx] is the
    // cow world the run starts on. dominantIdx is the body with the strongest
    // pull at the ship (drives camera/contact/beam); activeFieldIdx is the one
    // planet whose destructible field is currently live (lazy).
    std::vector<Planet> planets;
    int sunIdx = 0;
    int homeIdx = 1;
    int dominantIdx = 1;
    int activeFieldIdx = -1;
    Lander lander;
    View view;
    GameState state = PLAYING;

    std::vector<Cow> cows;
    int cargo = 0;
    int score = 0;

    Astronaut astronaut;
    bool spacewalking  = false;
    bool prevExitKey   = false;

    std::vector<Vec2> trajectory;   // reused coast-path buffer

    struct Star { float x, y, size, brightness; };
    std::vector<Star> stars;

    // Drifting atmospheric dust/spore motes at several parallax depth planes.
    // base clip pos + a depth (near planes move more), a sway phase, and size.
    struct Mote { float x, y, depth, phase, size; };
    std::vector<Mote> motes;

    // Occasional shooting star: a single light point travelling A->B on a far
    // background plane (drifts slightly with the ship), leaving a streak built
    // from its own recent positions that fades out behind it.
    struct ShootingStar {
        bool active = false;
        Vec2 head;           // current point, clip space
        Vec2 vel;            // clip space per second
        float life = 0.0f;   // seconds since spawn
        float maxLife = 1.0f;
        float timer = 3.0f;  // seconds until next spawn
        Vec2 spawnFocus;     // ship pos at spawn (for parallax drift)
        struct Tp { Vec2 p; float age; };
        std::vector<Tp> trail;   // recent head positions, oldest first
    } shootingStar;

    // Fullscreen shader passes (planet body, atmosphere, clouds).
    GLuint atmoProgram = 0;
    GLuint cloudProgram = 0;
    GLuint bodyProgram = 0;         // planet body: SDF fill + material + terminator
    GLuint fsVao = 0, fsVbo = 0;
    GLuint terrainTex = 0;          // 2048x1 R32F: terrain radius per angle (the silhouette)
    GLuint fieldTex = 0;            // R16F mirror of the TerrainField corner grid

    // Previous-frame key states for edge-triggered actions (lift off / restart).
    bool prevThrustKey = false;
    bool prevRestartKey = false;

    double startTime = 0;
    double lastFrameTime = 0;
    int width = 800;
    int height = 600;
};

static App app;

static Planet& homePlanet() { return app.planets[app.homeIdx]; }
static Planet& dominantPlanet() { return app.planets[app.dominantIdx]; }

// Body with the strongest pull at a point (the one you're "at"). The sun is
// excluded: nothing lands there, and the camera should never frame against it.
static int dominantBody(Vec2 p) {
    int best = app.homeIdx;
    float bestG = -1.0f;
    for (int i = 0; i < (int)app.planets.size(); i++) {
        const Planet& pl = app.planets[i];
        if (pl.isSun) continue;
        float dx = pl.center.x - p.x, dy = pl.center.y - p.y;
        float distSq = dx * dx + dy * dy;
        if (distSq < 1.0f) distSq = 1.0f;
        float g = pl.gravity / distSq;
        if (g > bestG) { bestG = g; best = i; }
    }
    return best;
}

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
        // Hooves (20*SCALE below center) rest on the blob shadow's contact
        // point, ~9 below the silhouette line (the ~40 degree ground view).
        float r = homePlanet().surfaceRadius(angle) + 7.0f;
        Cow cow;
        cow.spawn(homePlanet(), angle, r);
        app.cows.push_back(cow);
    }
}

static void resetGame() {
    app.lander.reset();
    app.lander.spawnOnPlanet(homePlanet());
    app.state = PLAYING;
    app.cargo = 0;
    app.score = 0;
    app.dominantIdx = app.homeIdx;
    spawnCows();
    app.spacewalking      = false;
    app.astronaut.active  = false;

    app.view.focusX = app.lander.pos.x;
    app.view.focusY = app.lander.pos.y;
    app.view.scale = gDebug.cameraGroundZoom;

    // Align the initial roll so gravity points screen-down.
    Vec2 g = app.lander.gravityAt(homePlanet(), app.lander.pos);
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
    Planet& p = dominantPlanet();
    float dx = l.pos.x - p.center.x;
    float dy = l.pos.y - p.center.y;
    float d = sqrtf(dx * dx + dy * dy);
    float angle = atan2d(dy, dx);
    float alt = d - p.surfaceRadius(angle);
    l.altitude = alt > 0 ? alt : 0;

    if (!l.active) return;
    if (alt <= l.radius) {
        // Speed is judged relative to the body — landing on an orbiting moon
        // means matching its velocity, not the world frame's.
        bool speedSafe = (l.vel - p.orbitVel).length() < Lander::LANDING_MAX_SPEED;
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

// Fullscreen quad covering clip space, shared by all the shader passes.
static void initShaderPasses() {
    app.atmoProgram = Shader::createProgram(kFsVert, kAtmoFrag);
    app.cloudProgram = Shader::createProgram(kFsVert, kCloudFrag);
    app.bodyProgram = Shader::createProgram(kFsVert, kBodyFrag);

    float quad[] = { -1, -1,  1, -1,  -1, 1,  -1, 1,  1, -1,  1, 1 };
    glGenVertexArrays(1, &app.fsVao);
    glGenBuffers(1, &app.fsVbo);
    glBindVertexArray(app.fsVao);
    glBindBuffer(GL_ARRAY_BUFFER, app.fsVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    // 2048xMAX_PLANETS float texture holding the terrain radius per angle (one
    // silhouette row per planet), so the sky/cloud/body shaders know exactly
    // where each ground meets its sky. High angular resolution (>> the
    // ~255-vertex terrain polygon) so the sampled silhouette faithfully follows
    // the real peaks instead of aliasing against them.
    glGenTextures(1, &app.terrainTex);
    glBindTexture(GL_TEXTURE_2D, app.terrainTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, 2048, SHADER_MAX_PLANETS, 0, GL_RED, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Density-field texture for the body pass; (re)allocated to the grid size
    // in uploadFieldTexture once the planet exists.
    glGenTextures(1, &app.fieldTex);
}

// Mirror the active planet's TerrainField corner grid into the R16F field
// texture. R16F is linearly filterable in core ES 3.0 (R32F is not), and
// half-float precision is ample where it matters: near the 0 iso-line.
static void uploadFieldTexture() {
    if (app.activeFieldIdx < 0) return;
    const TerrainField& f = app.planets[app.activeFieldIdx].field;
    glBindTexture(GL_TEXTURE_2D, app.fieldTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R16F, f.cornersX(), f.cornersY(), 0,
                 GL_RED, GL_FLOAT, f.corners().data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

// Re-upload just the corners a dig touched (UNPACK_ROW_LENGTH lets us copy the
// sub-rect straight out of the full CPU grid) — this is the whole GPU cost of
// a dig, replacing the old full mesh rebuild + VBO upload.
static void uploadFieldRegion(TerrainField::Rect rc) {
    if (rc.i1 < rc.i0 || rc.j1 < rc.j0 || app.activeFieldIdx < 0) return;
    const TerrainField& f = app.planets[app.activeFieldIdx].field;
    glBindTexture(GL_TEXTURE_2D, app.fieldTex);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, f.cornersX());
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, rc.i0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, rc.j0);
    glTexSubImage2D(GL_TEXTURE_2D, 0, rc.i0, rc.j0,
                    rc.i1 - rc.i0 + 1, rc.j1 - rc.j0 + 1,
                    GL_RED, GL_FLOAT, f.corners().data());
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
}

// Resample one planet's silhouette onto a uniform 2048-angle grid and upload it
// to that planet's row of the terrain texture. Call after its terrain changes.
static void uploadTerrainLUT(int planetIdx) {
    const int N = 2048;
    float lut[N];
    const Planet& p = app.planets[planetIdx];
    for (int k = 0; k < N; k++) {
        lut[k] = p.surfaceRadius(k * (360.0f / N));
    }
    glBindTexture(GL_TEXTURE_2D, app.terrainTex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, planetIdx, N, 1, GL_RED, GL_FLOAT, lut);
}

// Shared uniform setup: camera reconstruction params common to both passes.
static void setPassViewUniforms(GLuint pr) {
    glUniform2f(glGetUniformLocation(pr, "u_resolution"), (float)app.width, (float)app.height);
    glUniform2f(glGetUniformLocation(pr, "u_viewFocus"), app.view.focusX, app.view.focusY);
    glUniform2f(glGetUniformLocation(pr, "u_screenCenter"), app.width * 0.5f, app.height * 0.5f);
    glUniform1f(glGetUniformLocation(pr, "u_viewScale"), app.view.scale);
    glUniform1f(glGetUniformLocation(pr, "u_viewRotation"), app.view.rotation * DEG2RAD);
}

// Planet body pass: SDF fill + material + slope lighting + terminator, all per
// pixel from the field texture (see kBodyFrag). Drawn premultiplied-over so the
// rock occludes the stars and the sky shows through everywhere else.
static void drawBodyPass();   // defined after sunWorldPos (uses it)

// World position of the sun — the actual star body. It kinematically orbits the
// home planet (see init) at the old day-cycle rate, so the drifting terminator,
// sunsets, and cloud shading behave exactly as they did with the fake sun lamp.
// This single point is the light source for every pass AND where the visible
// sun is drawn, so they all agree.
static Vec2 sunWorldPos() {
    return app.planets[app.sunIdx].center;
}

// How strongly the sky + clouds fill the planet disk, as a function of the
// ship's altitude over the dominant body: 0 near the surface (so the ground
// stays clear in front of you) ramping to gDebug.spaceHaze once you're clearly
// out in space. This makes the atmosphere read as a full sphere with the planet
// showing through.
static float spaceOverlay() {
    const Planet& p = dominantPlanet();
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
    int count = (int)app.planets.size();
    if (count > N) count = N;
    for (int i = 0; i < count; i++) {
        const Planet& p = app.planets[i];
        pos[i * 2] = p.center.x; pos[i * 2 + 1] = p.center.y;
        if (p.isSun || !p.atmosphere) { outerR[i] = 0.0f; continue; }  // no deck
        float mountainTop = p.maxSurfaceRadius();
        outerR[i] = mountainTop + (p.atmosphereOuterRadius() - mountainTop) * gDebug.cloudHeight;
    }

    GLuint pr = app.cloudProgram;
    glUseProgram(pr);
    setPassViewUniforms(pr);
    glUniform1f(glGetUniformLocation(pr, "u_time"), t);
    glUniform1i(glGetUniformLocation(pr, "u_planetCount"), count);
    glUniform2fv(glGetUniformLocation(pr, "u_planetPos"), N, pos);
    glUniform1fv(glGetUniformLocation(pr, "u_planetOuterR"), N, outerR);
    glUniform1f(glGetUniformLocation(pr, "u_coverage"), gDebug.cloudCoverage);
    glUniform1f(glGetUniformLocation(pr, "u_falloff"), gDebug.cloudFalloff);
    glUniform1f(glGetUniformLocation(pr, "u_overlay"), spaceOverlay());
    glUniform1f(glGetUniformLocation(pr, "u_nightDarkness"), gDebug.nightDarkness);
    glUniform1f(glGetUniformLocation(pr, "u_softness"), gDebug.nightSoftness);
    Vec2 sunW = sunWorldPos();
    glUniform2f(glGetUniformLocation(pr, "u_sunPos"), sunW.x, sunW.y);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, app.terrainTex);
    glUniform1i(glGetUniformLocation(pr, "u_terrainTex"), 0);

    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);   // premultiplied "over"
    glBindVertexArray(app.fsVao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

// Planet body pass (see kBodyFrag): one fullscreen draw filling the rock from
// the density field, with material, slope lighting, the day/night terminator,
// and cave-interior backdrop. Runs before the cows so they stay readable on top.
static void drawBodyPass() {
    const int N = SHADER_MAX_PLANETS;
    float pos[N * 2] = {0}, maxR[N], baseR[N] = {0}, noiseBand[N] = {0};
    for (int i = 0; i < N; i++) maxR[i] = -1e9f;   // default: skip
    int count = (int)app.planets.size();
    if (count > N) count = N;
    for (int i = 0; i < count; i++) {
        const Planet& p = app.planets[i];
        pos[i * 2] = p.center.x; pos[i * 2 + 1] = p.center.y;
        if (p.isSun) continue;                      // the star is drawn separately
        maxR[i] = p.maxSurfaceRadius() + 2.0f;
        baseR[i] = p.baseRadius;
        noiseBand[i] = p.noiseIntensity;
    }

    GLuint pr = app.bodyProgram;
    glUseProgram(pr);
    setPassViewUniforms(pr);
    glUniform1i(glGetUniformLocation(pr, "u_planetCount"), count);
    glUniform2fv(glGetUniformLocation(pr, "u_planetPos"), N, pos);
    glUniform1fv(glGetUniformLocation(pr, "u_planetMaxR"), N, maxR);
    glUniform1fv(glGetUniformLocation(pr, "u_baseRadius"), N, baseR);
    glUniform1fv(glGetUniformLocation(pr, "u_noiseBand"), N, noiseBand);
    glUniform1i(glGetUniformLocation(pr, "u_activeField"), app.activeFieldIdx);
    glUniform1f(glGetUniformLocation(pr, "u_nightDarkness"), gDebug.nightDarkness);
    glUniform1f(glGetUniformLocation(pr, "u_softness"), gDebug.nightSoftness);
    Vec2 sunW = sunWorldPos();
    glUniform2f(glGetUniformLocation(pr, "u_sunPos"), sunW.x, sunW.y);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, app.terrainTex);
    glUniform1i(glGetUniformLocation(pr, "u_terrainTex"), 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, app.fieldTex);
    glUniform1i(glGetUniformLocation(pr, "u_fieldTex"), 1);
    glActiveTexture(GL_TEXTURE0);   // leave unit 0 active for the other passes
    if (app.activeFieldIdx >= 0) {
        const TerrainField& f = app.planets[app.activeFieldIdx].field;
        glUniform2f(glGetUniformLocation(pr, "u_fieldOrigin"), f.originX(), f.originY());
        glUniform1f(glGetUniformLocation(pr, "u_fieldCell"), f.cellSize());
        glUniform2f(glGetUniformLocation(pr, "u_fieldSize"), (float)f.cornersX(), (float)f.cornersY());
    } else {
        glUniform1f(glGetUniformLocation(pr, "u_fieldCell"), 250.0f);  // edge-feather scale only
        glUniform2f(glGetUniformLocation(pr, "u_fieldSize"), 1.0f, 1.0f);
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);   // premultiplied "over"
    glBindVertexArray(app.fsVao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

// One atmosphere disk (radius = atmosphere top), drawn twice per frame:
// mode 0 = the scattering sky band above the terrain (before the ridges, so
// distant ranges paint over the sky and the terrain paints over them — true
// painter's-order occlusion); mode 1 = the altitude-gated face fill below the
// terrain line (after the body, so the from-space sky sphere tints the rock).
static void drawAtmospherePass(float t, int mode) {
    const int N = SHADER_MAX_PLANETS;
    float pos[N * 2] = {0}, prad[N] = {0}, arad[N] = {0};
    int count = (int)app.planets.size();
    if (count > N) count = N;
    for (int i = 0; i < count; i++) {
        const Planet& p = app.planets[i];
        pos[i * 2] = p.center.x; pos[i * 2 + 1] = p.center.y;
        if (p.isSun || !p.atmosphere) continue;     // arad 0 -> shader skips it
        // baseR is the sky-gradient reference; atmoR is the atmosphere top. The
        // actual ground/sky boundary per angle comes from the silhouette rows.
        prad[i] = p.baseRadius;
        arad[i] = p.atmosphereOuterRadius();
    }

    GLuint pr = app.atmoProgram;
    glUseProgram(pr);
    setPassViewUniforms(pr);
    glUniform1f(glGetUniformLocation(pr, "u_time"), t);
    glUniform1i(glGetUniformLocation(pr, "u_planetCount"), count);
    glUniform2fv(glGetUniformLocation(pr, "u_planetPos"), N, pos);
    glUniform1fv(glGetUniformLocation(pr, "u_planetRadius"), N, prad);
    glUniform1fv(glGetUniformLocation(pr, "u_atmoRadius"), N, arad);
    glUniform1f(glGetUniformLocation(pr, "u_rayleighH"), gDebug.atmRayleighH);
    glUniform1f(glGetUniformLocation(pr, "u_rimH"), gDebug.atmRimH);
    glUniform1f(glGetUniformLocation(pr, "u_mieH"), gDebug.atmMieH);
    glUniform1f(glGetUniformLocation(pr, "u_sunIntensity"), gDebug.atmSunIntensity);
    glUniform1f(glGetUniformLocation(pr, "u_extinction"), gDebug.atmExtinction);
    glUniform1f(glGetUniformLocation(pr, "u_overlay"), spaceOverlay());
    glUniform1i(glGetUniformLocation(pr, "u_mode"), mode);
    Vec2 sunW = sunWorldPos();
    glUniform2f(glGetUniformLocation(pr, "u_sunPos"), sunW.x, sunW.y);
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

    // ---- Build the solar system -------------------------------------------
    app.planets.clear();
    app.planets.reserve(4);

    // [0] The sun. It kinematically orbits the home planet at the old fake
    // day-cycle rate (0.005 rad per shader-time unit ≈ 0.0143°/frame-unit), so
    // the drifting terminator/sunsets the atmosphere was tuned around carry
    // over unchanged. Physically silly, visually identical; flip the parent
    // once the home planet orbits for real.
    Planet sun;
    sun.isSun = true;
    sun.atmosphere = false;
    sun.generate(Vec2(0, -256000.0f), 12000.0f, 500.0f, 64, gDebug.planetDensity, 99);
    app.planets.push_back(sun);
    app.sunIdx = 0;

    // [1] Home: the starter cow world, ported tuning (radiusScale 4.0 * 8000
    // base, big noise span, dense sampling for a jagged silhouette). Pinned at
    // the origin while the rest of the system is tuned around it.
    Planet home;
    home.generate(Vec2(0, 0), 8000.0f * 4.0f, 2000.0f, 255, gDebug.planetDensity, 1337);
    app.planets.push_back(home);
    app.homeIdx = 1;
    app.planets[app.sunIdx].setOrbit(app.homeIdx, 256000.0f, 0.0143f, -90.0f);

    // [2] An airless moon orbiting the home planet — the first "background
    // planet" hanging in the sky, and the first body to fly to and land on.
    // Far enough out that its own surface gravity dominates the home pull.
    Planet moon;
    moon.atmosphere = false;
    moon.generate(Vec2(0, 0), 6000.0f, 600.0f, 160, gDebug.planetDensity, 4242);
    moon.setOrbit(app.homeIdx, 112000.0f, 0.010f, 35.0f);
    app.planets.push_back(moon);

    // Settle orbit positions before anything reads centers.
    for (Planet& p : app.planets) p.updateOrbit(app.planets, 0.0f);

    // Home starts with the live destructible field (the player spawns there).
    app.activeFieldIdx = app.homeIdx;
    homePlanet().ensureField();
    uploadFieldTexture();
    for (int i = 0; i < (int)app.planets.size(); i++) uploadTerrainLUT(i);

    // Static clip-space starfield (drawn behind everything).
    unsigned int s = 9871;
    auto rnd = [&s]() { s = s * 1664525u + 1013904223u; return (s >> 8) * (1.0f / 16777216.0f); };
    for (int i = 0; i < 320; i++) {
        app.stars.push_back({rnd() * 2.0f - 1.0f, rnd() * 2.0f - 1.0f,
                             0.0008f + rnd() * 0.0022f, 0.35f + rnd() * 0.65f});
    }
    // Dust motes across three depth planes (far/mid/near by `depth`).
    for (int i = 0; i < 90; i++) {
        float depth = 0.18f + rnd() * 0.82f;          // 0.18 far .. 1.0 near
        app.motes.push_back({rnd() * 2.0f - 1.0f, rnd() * 2.0f - 1.0f,
                             depth, rnd() * 6.2831853f,
                             0.0010f + depth * 0.0040f});
    }

    Input::init("#canvas");
    resetGame();

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    app.startTime = emscripten_get_now() / 1000.0;
    app.lastFrameTime = app.startTime;

    printf("=== Cow Abductor (C++/WebGL) ===\n");
    printf("W/Up - thrust | A/D or Left/Right - rotate\n");
}

// Carve the crust just under the ship's belly, then refresh the body mesh +
// silhouette LUT. Carving at the belly (not at the surface line) means a held
// press makes one crater; you deepen a shaft only by flying down into it, so the
// hole stays controllable instead of drilling to the core in one go.
static constexpr float DIG_RADIUS = 150.0f;
static void doDig() {
    if (!app.spacewalking) return;   // only the suited astronaut can build
    if (app.dominantIdx != app.activeFieldIdx) return;  // need the live field underfoot
    Astronaut& a = app.astronaut;
    Planet& p = dominantPlanet();
    float dx = p.center.x - a.pos.x;
    float dy = p.center.y - a.pos.y;
    float d  = sqrtf(dx * dx + dy * dy);
    if (d < 1.0f) return;
    Vec2 digPt(a.pos.x + (dx / d) * (Astronaut::RADIUS + DIG_RADIUS * 0.25f),
               a.pos.y + (dy / d) * (Astronaut::RADIUS + DIG_RADIUS * 0.25f));
    if (p.dig(digPt, DIG_RADIUS)) {
        uploadTerrainLUT(app.dominantIdx);
        uploadFieldRegion(p.field.digRect());   // just the brush's dirty rect
    }
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

static void pollInputAstronaut(float timeScale) {
    Astronaut& a = app.astronaut;
    Planet& p = dominantPlanet();

    bool left  = Input::isKeyDown(Input::KEY_A) || Input::isKeyDown(Input::KEY_LEFT);
    bool right = Input::isKeyDown(Input::KEY_D) || Input::isKeyDown(Input::KEY_RIGHT);
    if (a.grounded) {
        if (left)  a.walkLeft(p, timeScale);
        if (right) a.walkRight(p, timeScale);
    }

    bool jumpKey = Input::isKeyDown(Input::KEY_W) || Input::isKeyDown(Input::KEY_UP);
    if (jumpKey && a.grounded) a.jump(p);

    a.jetting = false;
    if (Input::isKeyDown(Input::KEY_SPACE)) a.jetpack(p, timeScale);

    // E digs only when far from the ship; close to the ship, E re-enters (edge
    // trigger in the main loop handles that case before this runs).
    float dToShip = dist(a.pos.x, a.pos.y, app.lander.pos.x, app.lander.pos.y);
    if (Input::isKeyDown(Input::KEY_E) && dToShip >= app.lander.radius * 4.0f) doDig();
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
    View& v = app.view;

    Vec2 focusPos = app.spacewalking ? app.astronaut.pos : app.lander.pos;
    Vec2 focusVel = app.spacewalking ? app.astronaut.vel : app.lander.vel;

    // Altitude-based framing, split at the atmosphere boundary (see updateView
    // in sketch.js). surfaceDistance is measured from the spherical base radius.
    const Planet& body = dominantPlanet();
    float d = dist(focusPos.x, focusPos.y, body.center.x, body.center.y);
    float surfaceDistance = d - body.baseRadius;
    float atmoThickness = body.atmosphereOuterRadius() - body.baseRadius;
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
    // Deep-space floor: low enough that cruising between bodies keeps the
    // whole neighborhood in frame (the old 0.02 was single-planet tuning).
    if (targetScale < 0.0008f) targetScale = 0.0008f;
    targetScale *= v.userZoom;

    v.scale += (targetScale - v.scale) * View::ZOOM_EASE * timeScale;

    // Velocity-predictive follow so easing only corrects displacement.
    v.focusX += focusVel.x * timeScale + (focusPos.x - v.focusX) * View::FOLLOW_EASE * timeScale;
    v.focusY += focusVel.y * timeScale + (focusPos.y - v.focusY) * View::FOLLOW_EASE * timeScale;

    // Roll so the dominant body's gravity points screen-down (single body, so
    // standing on a moon doesn't tilt toward the planet pulling from afar).
    Vec2 g = app.lander.gravityAt(dominantPlanet(), focusPos);
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
    app.lander.predictTrajectory(app.planets, app.trajectory);
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

// The visible star: a bright core disc inside a stacked additive corona (port
// of the p5 sun corona), drawn in world space at the actual sun body so the
// light direction, the sky shading, and the thing you see all agree. A small
// screen-pixel floor keeps it visible as a glint from across the system.
static void drawSun(const Mat3& viewMat) {
    const Planet& sun = app.planets[app.sunIdx];
    float worldPerPx = 1.0f / app.view.scale;
    float coreR = sun.baseRadius * 0.95f;
    if (coreR < 3.0f * worldPerPx) coreR = 3.0f * worldPerPx;   // distant glint
    Renderer2D& r = app.renderer;
    r.begin(viewMat, app.width, app.height);
    r.setBlend(Renderer2D::Blend::Additive);
    // Corona: layered warm discs, pow-curved so the glow hugs the core.
    const int layers = 10;
    float outer = coreR * 2.1f;
    for (int i = 0; i < layers; i++) {
        float linear = (float)i / (layers - 1);
        float tt = powf(linear, 2.5f);
        float rad = outer - (outer - coreR) * tt;
        r.fill(1.0f, 0.86f, 0.39f, 14.0f / 255.0f);
        r.circle(sun.center.x, sun.center.y, rad, 48);
    }
    r.fill(1.0f, 0.96f, 0.80f, 1.0f);
    r.circle(sun.center.x, sun.center.y, coreR, 48);
    r.setBlend(Renderer2D::Blend::Normal);
    r.end();
}

// Lazy destructible fields: only the planet you're near keeps a live density
// grid (memory is O(radius^2) per field). Engage when the player closes within
// ~1.5x the body's sky bound, release the old one when switching; recorded digs
// replay on the next visit. Hysteresis (engage at 1.5x, keep until someone else
// engages) avoids thrash at the boundary.
static void updateActiveField() {
    Vec2 sp = app.spacewalking ? app.astronaut.pos : app.lander.pos;
    int want = app.activeFieldIdx;
    float bestD = 1e30f;
    for (int i = 0; i < (int)app.planets.size(); i++) {
        const Planet& p = app.planets[i];
        if (p.isSun) continue;
        float d = dist(sp.x, sp.y, p.center.x, p.center.y);
        float engage = (p.maxSurfaceRadius() > p.atmosphereOuterRadius()
                            ? p.maxSurfaceRadius() : p.atmosphereOuterRadius()) * 1.5f;
        if (d < engage && d < bestD) { bestD = d; want = i; }
    }
    if (want == app.activeFieldIdx) return;

    if (app.activeFieldIdx >= 0) {
        app.planets[app.activeFieldIdx].releaseField();
        uploadTerrainLUT(app.activeFieldIdx);   // LUT falls back to the analytic shape
    }
    app.activeFieldIdx = want;
    if (want >= 0) {
        app.planets[want].ensureField();        // seeds grid + replays recorded digs
        uploadFieldTexture();
        uploadTerrainLUT(want);
    }
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
    Vec2 tip = app.lander.pos + nose * (20.0f * s);
    Vec2 hl  = tip - nose * (12.0f * s) + perp * (14.0f * s);
    Vec2 hr  = tip - nose * (12.0f * s) - perp * (14.0f * s);

    r.setBlend(Renderer2D::Blend::Additive);
    r.fill(1.0f, 1.0f, 1.0f, 0.15f);   // outer halo
    r.line(tip.x, tip.y, hl.x, hl.y, 7.0f * s);
    r.line(tip.x, tip.y, hr.x, hr.y, 7.0f * s);
    r.fill(1.0f, 1.0f, 1.0f, 0.45f);   // mid glow
    r.line(tip.x, tip.y, hl.x, hl.y, 3.5f * s);
    r.line(tip.x, tip.y, hr.x, hr.y, 3.5f * s);
    r.fill(1.0f, 1.0f, 1.0f, 0.95f);   // bright core
    r.line(tip.x, tip.y, hl.x, hl.y, 1.5f * s);
    r.line(tip.x, tip.y, hr.x, hr.y, 1.5f * s);
    r.setBlend(Renderer2D::Blend::Normal);
}

static float smoothstepf(float e0, float e1, float x) {
    float t = (x - e0) / (e1 - e0);
    t = t < 0 ? 0 : (t > 1 ? 1 : t);
    return t * t * (3.0f - 2.0f * t);
}

// Soft elliptical ground shadow under an entity: projected radially onto the
// terrain, oriented along the local tangent, slid away from the sun as the
// caster rises, and faded out with altitude. The grounding cue that makes a
// hovering/landed sprite read as sitting ON the surface instead of pasted on
// the silhouette rim.
static void drawBlobShadow(const Planet& p, Vec2 pos, float size, float maxAlt, Vec2 toSun) {
    float dx = pos.x - p.center.x, dy = pos.y - p.center.y;
    float distC = sqrtf(dx * dx + dy * dy);
    if (distC < 1.0f) return;
    float ang = atan2d(dy, dx);
    float surfR = p.surfaceRadius(ang);
    float alt = distC - surfR;
    if (alt < 0.0f) alt = 0.0f;
    if (alt > maxAlt) return;

    Vec2 outward(cosd(ang), sind(ang));
    Vec2 tangent(-outward.y, outward.x);
    // No sun, no shadow: fade with the same terminator ramp as the body.
    float sunDot = outward.x * toSun.x + outward.y * toSun.y;
    float lit = smoothstepf(-gDebug.nightSoftness, gDebug.nightSoftness, sunDot);
    if (lit <= 0.02f) return;

    // Slide along the ground away from the sun as the caster rises (clamped so
    // a high hover doesn't push the shadow absurdly far).
    float lateral = -(tangent.x * toSun.x + tangent.y * toSun.y);
    float radial = sunDot < 0.3f ? 0.3f : sunDot;
    float shift = lateral / radial * alt;
    float maxShift = size * 3.0f;
    if (shift > maxShift) shift = maxShift;
    if (shift < -maxShift) shift = -maxShift;

    float fade = 1.0f - alt / maxAlt;
    float rx = size * (1.0f + (alt / maxAlt) * 0.9f);   // spreads as it rises
    // Ground-plane ellipse language: every circle lying on the ground (shadows,
    // pads, craters, splats) keeps height ~64% of width — a ~40 degree view
    // angle above the surface (sin 40). The contact point sits one ellipse
    // half-height BELOW the terrain silhouette line, so the ground reads as
    // extending toward the viewer in front of whatever stands on it.
    float ry = rx * 0.64f;
    float a = 0.42f * fade * fade * lit;

    Renderer2D& r = app.renderer;
    float drop = size * 0.64f;   // contact point below the silhouette line
    Vec2 ground(p.center.x + outward.x * (surfR - drop),
                p.center.y + outward.y * (surfR - drop));
    r.pushMatrix();
    r.translate(ground.x + tangent.x * shift, ground.y + tangent.y * shift);
    r.rotate(ang + 90.0f);   // local +x along the surface tangent
    // One smooth shadow: a flat dark core that fades continuously to nothing
    // at the rim (gradient ring of triangleV pairs), instead of stacked
    // ellipses whose alpha steps read as concentric circles.
    {
        const int N = 24;
        const float coreK = 0.45f;   // flat core radius as a fraction of rx/ry
        Renderer2D::Color dark{0, 0, 0, a};
        Renderer2D::Color clear{0, 0, 0, 0};
        Vec2 c(0, 0);
        for (int i = 0; i < N; i++) {
            float a0 = (float)i / N * 6.2831853f;
            float a1 = (float)(i + 1) / N * 6.2831853f;
            Vec2 in0(cosf(a0) * rx * coreK, sinf(a0) * ry * coreK);
            Vec2 in1(cosf(a1) * rx * coreK, sinf(a1) * ry * coreK);
            Vec2 out0(cosf(a0) * rx, sinf(a0) * ry);
            Vec2 out1(cosf(a1) * rx, sinf(a1) * ry);
            r.triangleV(c, dark, in0, dark, in1, dark);          // core wedge
            r.triangleV(in0, dark, out0, clear, out1, clear);    // fade ring
            r.triangleV(in0, dark, out1, clear, in1, dark);
        }
    }
    r.popMatrix();
}

// Ground shadows for everything that walks or flies near the surface. Must run
// inside an active renderer batch, before the entities themselves. Cows live on
// the home planet; the ship/astronaut shadow falls on whichever body dominates.
static void drawEntityShadows() {
    Vec2 sunW = sunWorldPos();
    Vec2 toSunHome = (sunW - homePlanet().center).normalize();
    for (const Cow& cow : app.cows) {
        if (cow.state == Cow::State::Stowed) continue;
        drawBlobShadow(homePlanet(), cow.pos, 14.0f, 320.0f, toSunHome);
    }
    const Planet& dom = dominantPlanet();
    Vec2 toSunDom = (sunW - dom.center).normalize();
    drawBlobShadow(dom, app.lander.pos, app.lander.radius * 1.5f, 650.0f, toSunDom);
    if (app.spacewalking)
        drawBlobShadow(dom, app.astronaut.pos, Astronaut::RADIUS * 1.7f, 320.0f, toSunDom);
}

// Distant ridge silhouettes: haze-tinted mountain ranges that follow a
// fraction of the player's surface angle, so in screen space the horizon
// slides with you but slower than the foreground — depth without a second
// camera. Drawn AFTER the atmosphere pass (the daytime sky is opaque above the
// terrain, so anything earlier is painted over — this is where p5 drew them
// too). Each segment is clipped per-vertex to the band between the real
// terrain silhouette and the ridge line, so only the slivers cresting the
// horizon render and the planet face / entities are never covered. Per-vertex
// shading: night darkening plus an ember-warm sunset band at the terminator.
// Long cast shadows from terrain peaks. Over a window around the view we run a
// 1D horizon sweep: marching away from the sun along the surface, a running
// "shadow ceiling" drops by the sun's apparent slope each step and is lifted by
// any peak; ground below the ceiling is in that peak's shadow. The shadowed
// angular sectors are darkened from the silhouette down, so low-sun ranges throw
// long shadows across the valleys behind them — a strong 3D + time-of-day cue.
static void drawTerrainShadows(const Mat3& viewMat) {
    const Planet& p = dominantPlanet();
    if (p.isSun) return;
    float strength = 1.0f - spaceOverlay();
    if (strength <= 0.01f) return;

    Vec2 focus = app.spacewalking ? app.astronaut.pos : app.lander.pos;
    float viewAngle = atan2d(focus.y - p.center.y, focus.x - p.center.x);
    Vec2 toSun = (sunWorldPos() - p.center).normalize();
    float sunAng = atan2d(toSun.y, toSun.x);

    // Sun geometry at the player: radial (elevation) and tangential components.
    Vec2 up(cosd(viewAngle), sind(viewAngle));
    Vec2 tang(-up.y, up.x);
    float sunRadial = up.x * toSun.x + up.y * toSun.y;       // elevation
    float sunTang = tang.x * toSun.x + tang.y * toSun.y;     // along +angle
    if (sunRadial <= 0.05f) return;                          // sun ~down: skip
    if (fabsf(sunTang) < 0.10f) return;                      // sun overhead: no long cast
    float sunSlope = sunRadial / fabsf(sunTang);             // rise per arc length

    // Sample a window around the view; sweep AWAY from the sun.
    const float span = 36.0f;                                // degrees each side
    const int M = 240;
    float dAng = (2.0f * span) / M;                          // deg per step
    float ds = (dAng * 3.14159265f / 180.0f) * p.baseRadius; // arc length per step
    float sunRel = sunAng - viewAngle;
    while (sunRel > 180.0f) sunRel -= 360.0f;
    while (sunRel < -180.0f) sunRel += 360.0f;
    int dirSign = (sunRel > 0.0f) ? +1 : -1;

    static float hbuf[M + 1];
    static float sf[M + 1];
    static float ang[M + 1];
    for (int i = 0; i <= M; i++) {
        ang[i] = viewAngle - span + dAng * i;
        hbuf[i] = p.surfaceRadius(ang[i]) - p.baseRadius;
    }
    // Sweep from the sun-ward end toward the far end.
    int start = dirSign > 0 ? M : 0;
    int end   = dirSign > 0 ? -1 : M + 1;
    int step  = dirSign > 0 ? -1 : +1;
    float ceiling = -1e30f;
    for (int i = start; i != end; i += step) {
        ceiling -= sunSlope * ds;
        if (hbuf[i] >= ceiling) { ceiling = hbuf[i]; sf[i] = 0.0f; }
        else sf[i] = (ceiling - hbuf[i]) / (p.noiseIntensity * 0.45f);
        if (sf[i] > 1.0f) sf[i] = 1.0f;
    }

    float skirtR = p.baseRadius * 0.90f;
    Renderer2D& r = app.renderer;
    r.begin(viewMat, app.width, app.height);
    for (int i = 0; i < M; i++) {
        float a1 = ang[i], a2 = ang[i + 1];
        float lit1 = smoothstepf(0.0f, gDebug.nightSoftness, cosd(a1) * toSun.x + sind(a1) * toSun.y);
        float lit2 = smoothstepf(0.0f, gDebug.nightSoftness, cosd(a2) * toSun.x + sind(a2) * toSun.y);
        float al1 = sf[i] * 0.42f * lit1 * strength;
        float al2 = sf[i + 1] * 0.42f * lit2 * strength;
        if (al1 < 0.004f && al2 < 0.004f) continue;
        float rt1 = p.surfaceRadius(a1), rt2 = p.surfaceRadius(a2);
        Renderer2D::Color c1{0.04f, 0.05f, 0.09f, al1}, c2{0.04f, 0.05f, 0.09f, al2};
        Vec2 t1(p.center.x + cosd(a1) * rt1, p.center.y + sind(a1) * rt1);
        Vec2 t2(p.center.x + cosd(a2) * rt2, p.center.y + sind(a2) * rt2);
        Vec2 b1(p.center.x + cosd(a1) * skirtR, p.center.y + sind(a1) * skirtR);
        Vec2 b2(p.center.x + cosd(a2) * skirtR, p.center.y + sind(a2) * skirtR);
        r.triangleV(b1, c1, t1, c1, t2, c2);
        r.triangleV(b1, c1, t2, c2, b2, c2);
    }
    r.end();
}

// A cosmetic looming gas giant high in the sky — a big banded disc that swings
// slowly across the view as you orbit (a fixed celestial azimuth), dimmed on
// the night side. Purely visual (no gravity/simulation); a large body at real
// scale in the distance is a strong depth anchor. Drawn in screen space behind
// the clouds/ridges. Slices are circle-clipped so bands stay inside the disc.
static void drawLoomingBody(float time) {
    const Planet& p = dominantPlanet();
    if (p.isSun) return;
    float strength = 1.0f - spaceOverlay();
    if (strength <= 0.01f) return;

    Vec2 focus = app.spacewalking ? app.astronaut.pos : app.lander.pos;
    float viewAngle = atan2d(focus.y - p.center.y, focus.x - p.center.x);
    const float azimuth = -52.0f;          // fixed direction in the sky
    float diff = viewAngle - azimuth;
    while (diff > 180.0f) diff -= 360.0f;
    while (diff < -180.0f) diff += 360.0f;
    float sx = diff / 64.0f;               // screen-x: on screen within ~±64deg
    if (fabsf(sx) > 1.6f) return;
    const float sy = 0.55f, R = 0.36f;
    float aspect = (float)app.width / (float)app.height;

    Vec2 toSun = (sunWorldPos() - p.center).normalize();
    Vec2 up(cosd(viewAngle), sind(viewAngle));
    float day = up.x * toSun.x + up.y * toSun.y;
    float lit = 0.30f + 0.70f * smoothstepf(-0.35f, 0.35f, day);
    float a = strength;

    Renderer2D& r = app.renderer;
    r.beginScreenSpace();
    // Soft atmosphere halo.
    for (int i = 5; i >= 1; i--) {
        float rr = R * (1.0f + 0.12f * i);
        r.fill(0.52f * lit, 0.60f * lit, 0.76f * lit, 0.045f * a);
        r.ellipse(sx, sy, rr / aspect, rr, 28);
    }
    // Banded disc: horizontal slices, each clipped to the circle's half-width.
    const int slices = 16;
    for (int s = 0; s < slices; s++) {
        float fy = ((float)s / (slices - 1)) * 2.0f - 1.0f;   // -1..1
        float dy = fy * R;
        float hw = R * R - dy * dy;
        hw = hw > 0 ? sqrtf(hw) : 0;
        float band = 0.5f + 0.5f * sinf(fy * 5.3f + time * 0.04f)
                   + 0.25f * sinf(fy * 11.0f - time * 0.02f);
        if (band < 0) band = 0; if (band > 1) band = 1;
        float tr = 0.46f + (0.70f - 0.46f) * band;
        float tg = 0.37f + (0.54f - 0.37f) * band;
        float tb = 0.30f + (0.42f - 0.30f) * band;
        r.fill(tr * lit, tg * lit, tb * lit, a);
        r.ellipse(sx, sy + dy, hw / aspect, (R / slices) * 1.5f, 26);
    }
    r.endScreenSpace();
}

// A far, hazy cloud band high in the sky behind the ridges — a soft undulating
// ribbon at the top of the atmosphere that adds a whole plane between the flat
// sky and the farthest hills. Anchored to world angle (so flying reveals new
// puffs) but pinned overhead in radius (far things barely shift). Terminator-
// shaded and faded out toward space like the rest of the surface dressing.
static void drawFarClouds(const Mat3& viewMat, float time) {
    const Planet& p = dominantPlanet();
    if (p.isSun || !p.atmosphere) return;
    float strength = 1.0f - spaceOverlay();
    if (strength <= 0.01f) return;

    Vec2 focus = app.spacewalking ? app.astronaut.pos : app.lander.pos;
    Vec2 toSun = (sunWorldPos() - p.center).normalize();
    float viewAngle = atan2d(focus.y - p.center.y, focus.x - p.center.x);
    float thickness = p.atmosphereOuterRadius() - p.baseRadius;
    if (thickness < 1.0f) return;
    float R0 = p.baseRadius + thickness * 0.62f;   // inner edge (alpha 0)
    float Rm = p.baseRadius + thickness * 0.78f;   // bright middle
    float R1 = p.baseRadius + thickness * 0.95f;   // outer edge (alpha 0)
    const float span = 46.0f;                      // degrees each side of view
    const int seg = 96;

    auto puff = [&](float ang) {
        float n = 0.5f + 0.5f * sinf(ang * 0.05f + time * 0.02f)
                + 0.32f * sinf(ang * 0.13f - time * 0.015f);
        n = n < 0 ? 0 : n;
        return powf(n * 0.62f, 1.6f);              // sparse, soft gaps
    };
    auto shadeAt = [&](float ang) {
        float sunDot = cosd(ang) * toSun.x + sind(ang) * toSun.y;
        return gDebug.nightDarkness + (1.0f - gDebug.nightDarkness) *
               smoothstepf(-gDebug.nightSoftness, gDebug.nightSoftness, sunDot);
    };

    Renderer2D& r = app.renderer;
    r.begin(viewMat, app.width, app.height);
    for (int i = 0; i < seg; i++) {
        float a1 = viewAngle - span + 2.0f * span * ((float)i / seg);
        float a2 = viewAngle - span + 2.0f * span * ((float)(i + 1) / seg);
        float p1 = puff(a1) * shadeAt(a1) * strength;
        float p2 = puff(a2) * shadeAt(a2) * strength;
        auto col = [&](float ang, float pa, float sh) {
            // pale cloud, slightly warmed toward the sun side
            return Renderer2D::Color{0.84f * sh, 0.86f * sh, 0.92f * sh, pa * 0.55f};
        };
        Renderer2D::Color c1 = col(a1, p1, shadeAt(a1)), c2 = col(a2, p2, shadeAt(a2));
        Renderer2D::Color clr1{c1.r, c1.g, c1.b, 0.0f}, clr2{c2.r, c2.g, c2.b, 0.0f};
        Vec2 i1(p.center.x + cosd(a1) * R0, p.center.y + sind(a1) * R0);
        Vec2 i2(p.center.x + cosd(a2) * R0, p.center.y + sind(a2) * R0);
        Vec2 m1(p.center.x + cosd(a1) * Rm, p.center.y + sind(a1) * Rm);
        Vec2 m2(p.center.x + cosd(a2) * Rm, p.center.y + sind(a2) * Rm);
        Vec2 o1(p.center.x + cosd(a1) * R1, p.center.y + sind(a1) * R1);
        Vec2 o2(p.center.x + cosd(a2) * R1, p.center.y + sind(a2) * R1);
        // inner edge -> middle
        r.triangleV(i1, clr1, m1, c1, m2, c2);
        r.triangleV(i1, clr1, m2, c2, i2, clr2);
        // middle -> outer edge
        r.triangleV(m1, c1, o1, clr1, o2, clr2);
        r.triangleV(m1, c1, o2, clr2, m2, c2);
    }
    r.end();
}

// Volumetric light shafts (god rays): soft additive beams raking through the
// atmosphere parallel to the sun direction. Center-bright, edge-transparent,
// tapering from the sky down toward the ground, drifting slowly. Only on the
// lit side (sun above the local horizon), brightest at grazing "golden-hour"
// sun angles, and faded out on the climb to space. Drawn in the sky before the
// body so the terrain occludes their lower ends, like real shafts hitting ground.
static void drawLightShafts(const Mat3& viewMat, float time) {
    const Planet& p = dominantPlanet();
    if (p.isSun) return;
    float strength = 1.0f - spaceOverlay();
    if (strength <= 0.01f) return;

    Vec2 focus = app.spacewalking ? app.astronaut.pos : app.lander.pos;
    Vec2 toSun = (sunWorldPos() - p.center).normalize();
    float viewAngle = atan2d(focus.y - p.center.y, focus.x - p.center.x);
    Vec2 up(cosd(viewAngle), sind(viewAngle));
    float dayMask = up.x * toSun.x + up.y * toSun.y;    // sun height over horizon
    if (dayMask <= 0.02f) return;                        // sun down: no shafts
    float grazeBoost = 0.55f + 0.45f * (1.0f - dayMask); // stronger at low sun
    float vis = strength * dayMask * grazeBoost;

    Vec2 d(-toSun.x, -toSun.y);          // ray travel: away from the sun
    Vec2 perp(-d.y, d.x);
    float thickness = p.atmosphereOuterRadius() - p.baseRadius;
    if (thickness < 1.0f) return;
    float len = thickness * 2.6f;
    float worldPerPx = 1.0f / app.view.scale;
    float halfW = app.width * worldPerPx * 0.7f;

    Renderer2D& r = app.renderer;
    r.begin(viewMat, app.width, app.height);
    r.setBlend(Renderer2D::Blend::Additive);
    const int N = 9;
    for (int i = 0; i < N; i++) {
        float f = (float)i / (N - 1) - 0.5f;             // -0.5..0.5 across view
        float drift = sinf(time * 0.05f + i * 1.7f) * halfW * 0.09f;
        float off = f * (2.0f * halfW) + drift;
        Vec2 c(focus.x + perp.x * off, focus.y + perp.y * off);
        float w = thickness * 0.10f * (0.55f + 0.5f * sinf(time * 0.07f + i * 2.3f));
        if (w < thickness * 0.03f) w = thickness * 0.03f;
        Vec2 a0(c.x - d.x * len * 0.5f, c.y - d.y * len * 0.5f);  // toward sun (sky)
        Vec2 a1(c.x + d.x * len * 0.5f, c.y + d.y * len * 0.5f);  // toward ground
        float baseA = 0.06f * vis * (0.7f + 0.3f * sinf(time * 0.09f + i));
        Renderer2D::Color cNear{1.0f, 0.93f, 0.74f, baseA};
        Renderer2D::Color cFar{1.0f, 0.93f, 0.74f, baseA * 0.18f};
        Renderer2D::Color edge{1.0f, 0.93f, 0.74f, 0.0f};
        Vec2 e0m(a0.x - perp.x * w, a0.y - perp.y * w), e0p(a0.x + perp.x * w, a0.y + perp.y * w);
        Vec2 e1m(a1.x - perp.x * w, a1.y - perp.y * w), e1p(a1.x + perp.x * w, a1.y + perp.y * w);
        r.triangleV(e0m, edge, a0, cNear, a1, cFar);     // left half
        r.triangleV(e0m, edge, a1, cFar, e1m, edge);
        r.triangleV(a0, cNear, e0p, edge, e1p, edge);    // right half
        r.triangleV(a0, cNear, e1p, edge, a1, cFar);
    }
    r.setBlend(Renderer2D::Blend::Normal);
    r.end();
}

// foreground=false draws the distant haze layers (behind the body, painter's
// occlusion); foreground=true draws the near framing plane (over the body, so
// it passes in front of the terrain). Called in two passes around the body.
static void drawRidges(const Mat3& viewMat, bool foreground) {
    Renderer2D& r = app.renderer;
    Vec2 focus = app.spacewalking ? app.astronaut.pos : app.lander.pos;
    Vec2 sunW = sunWorldPos();
    // Ridges are a surface-view dressing: fade them out on the climb to space
    // along with the face fill, so the limb doesn't grow a colored fringe.
    float strength = 1.0f - spaceOverlay();
    if (strength <= 0.01f) return;

    // The pastel haze the layers dissolve into (warm cream-blue, not pure
    // blue) — the Summer Afternoon trick: distance reads as light, not grey.
    const float hazeR = 168.0f / 255.0f, hazeG = 182.0f / 255.0f, hazeB = 208.0f / 255.0f;

    r.begin(viewMat, app.width, app.height);
    for (const Planet& p : app.planets) {
        if (p.ridges.empty()) continue;
        float viewAngle = atan2d(focus.y - p.center.y, focus.x - p.center.x);
        Vec2 toSun = (sunW - p.center).normalize();
        float pRad = sqrtf((focus.x - p.center.x) * (focus.x - p.center.x) +
                           (focus.y - p.center.y) * (focus.y - p.center.y));
        // Soft "hole" in the foreground around the player so it never hides
        // them: fade toward transparent where the foreground is BOTH near the
        // player's angle AND rising in front of (crest above) the player. Purely
        // angular, so the hole tracks the player at any zoom. 1 = fully opaque.
        auto occFade = [&](float ang, float topR) -> float {
            if (!foreground) return 1.0f;
            float da = ang - viewAngle;
            while (da > 180.0f) da -= 360.0f;
            while (da < -180.0f) da += 360.0f;
            float holeHalf = (520.0f / (pRad > 1.0f ? pRad : 1.0f)) * 57.2957795f;
            float angProx = 1.0f - smoothstepf(holeHalf, holeHalf * 2.4f, fabsf(da));
            float cover = smoothstepf(pRad - 80.0f, pRad + 80.0f, topR);
            return 1.0f - 0.86f * angProx * cover;
        };
        for (const auto& layer : p.ridges) {
            if (layer.foreground != foreground) continue;
            // Layer tint faded toward the sky haze by distance (skyMix).
            float cr = layer.tintR + (hazeR - layer.tintR) * layer.skyMix;
            float cg = layer.tintG + (hazeG - layer.tintG) * layer.skyMix;
            float cb = layer.tintB + (hazeB - layer.tintB) * layer.skyMix;
            float shift = viewAngle * layer.follow;

            // ONE flat color per layer (see docs/art-style.md): the layer tint
            // hazed by distance, darkened across the terminator, and hue-
            // shifted to ember inside the dusk band. No gradients, no rim.
            auto colorAt = [&](float angle) {
                float sunDot = cosd(angle) * toSun.x + sind(angle) * toSun.y;
                float shade = gDebug.nightDarkness +
                              (1.0f - gDebug.nightDarkness) *
                                  smoothstepf(-gDebug.nightSoftness, gDebug.nightSoftness, sunDot);
                float dusk = expf(-sunDot * sunDot * 6.0f) * layer.skyMix * shade;
                float r0 = cr * shade, g0 = cg * shade, b0 = cb * shade;
                r0 += (1.05f * shade - r0) * dusk;
                g0 += (0.46f * shade - g0) * dusk * 0.85f;
                b0 += (0.24f * shade - b0) * dusk * 0.85f;
                return Renderer2D::Color{r0, g0, b0, layer.alpha * strength};
            };

            // Full unclipped bands: crest line down to a fixed skirt radius
            // well below any terrain. The body pass paints the rock over the
            // lower parts (painter's-order occlusion), so the visible color
            // field is continuous everywhere — no terrain reference, no seams,
            // and nothing shifts when the player (and the parallax) moves.
            float skirtR = p.baseRadius * 0.85f;
            for (size_t i = 0; i + 1 < layer.points.size(); i++) {
                float a1 = layer.points[i].angle + shift;
                float a2 = layer.points[i + 1].angle + shift;
                float rt1 = layer.points[i].r + layer.lift;
                float rt2 = layer.points[i + 1].r + layer.lift;
                Vec2 t1(p.center.x + cosd(a1) * rt1, p.center.y + sind(a1) * rt1);
                Vec2 t2(p.center.x + cosd(a2) * rt2, p.center.y + sind(a2) * rt2);
                Vec2 b1(p.center.x + cosd(a1) * skirtR, p.center.y + sind(a1) * skirtR);
                Vec2 b2(p.center.x + cosd(a2) * skirtR, p.center.y + sind(a2) * skirtR);
                Renderer2D::Color c1 = colorAt(a1);
                Renderer2D::Color c2 = colorAt(a2);
                c1.a *= occFade(a1, rt1);
                c2.a *= occFade(a2, rt2);
                r.triangleV(b1, c1, t1, c1, t2, c2);
                r.triangleV(b1, c1, t2, c2, b2, c2);
            }
        }
    }
    r.end();
}

// Silhouette clutter (planet.props): foreground props on the near framing plane
// (drawn closest, big and swaying) and ground tufts pinned to the real surface.
// `foreground` selects which set this pass draws. Dark shapes, terminator-
// shaded, faded out on the climb to space like the ridges. Local frame matches
// the cow/astronaut convention: rotate(angle+90) puts +Y toward the planet, so
// the props grow along -Y (outward, away from the center).
static void drawProps(const Mat3& viewMat, float time, bool foreground) {
    Renderer2D& r = app.renderer;
    Vec2 focus = app.spacewalking ? app.astronaut.pos : app.lander.pos;
    Vec2 sunW = sunWorldPos();
    float strength = 1.0f - spaceOverlay();
    if (strength <= 0.01f) return;

    r.begin(viewMat, app.width, app.height);
    for (const Planet& p : app.planets) {
        if (p.isSun || p.props.empty()) continue;
        float viewAngle = atan2d(focus.y - p.center.y, focus.x - p.center.x);
        Vec2 toSun = (sunW - p.center).normalize();
        float pRad = sqrtf((focus.x - p.center.x) * (focus.x - p.center.x) +
                           (focus.y - p.center.y) * (focus.y - p.center.y));
        for (const Planet::Prop& pr : p.props) {
            if (pr.foreground != foreground) continue;
            float drawAngle = pr.angle + viewAngle * pr.follow;
            // Cull to a window around the view: only what's on screen draws.
            float da = drawAngle - viewAngle;
            while (da > 180.0f) da -= 360.0f;
            while (da < -180.0f) da += 360.0f;
            if (fabsf(da) > 32.0f) continue;

            float baseR = foreground ? p.baseRadius + p.noiseIntensity * pr.radialFrac
                                     : p.surfaceRadius(pr.angle);
            float sunDot = cosd(drawAngle) * toSun.x + sind(drawAngle) * toSun.y;
            float shade = gDebug.nightDarkness + (1.0f - gDebug.nightDarkness) *
                          smoothstepf(-gDebug.nightSoftness, gDebug.nightSoftness, sunDot);
            float tone = foreground ? 0.10f : 0.17f;
            float S = pr.scale * (foreground ? 260.0f : 22.0f);

            // Same player "hole" as the foreground ridges: fade foreground props
            // near the player's angle when they'd stand in front of them.
            float alpha = strength;
            if (foreground) {
                float holeHalf = (520.0f / (pRad > 1.0f ? pRad : 1.0f)) * 57.2957795f;
                float angProx = 1.0f - smoothstepf(holeHalf, holeHalf * 2.4f, fabsf(da));
                float cover = smoothstepf(pRad - 80.0f, pRad + 80.0f, baseR + S);
                alpha *= 1.0f - 0.86f * angProx * cover;
            }
            r.fill(tone * shade, tone * shade, (tone + 0.03f) * shade, alpha);

            Vec2 base(p.center.x + cosd(drawAngle) * baseR,
                      p.center.y + sind(drawAngle) * baseR);
            float sway = sinf(time * 0.6f + pr.phase) * (foreground ? 4.0f : 2.5f);

            r.pushMatrix();
            r.translate(base.x, base.y);
            r.rotate(drawAngle + 90.0f + sway);   // +Y toward center; grow along -Y
            if (pr.kind == 0) {
                // Spire: tall narrow blade plus a shorter side spur.
                r.triangle(-0.13f * S, 0, 0, -1.0f * S, 0.13f * S, 0);
                r.triangle(-0.24f * S, 0, -0.07f * S, -0.42f * S, 0.05f * S, 0);
            } else {
                // Frond/tuft: a fan of tapering blades.
                for (int b = -2; b <= 2; b++) {
                    float bl = (1.0f - fabsf((float)b) * 0.16f) * S;
                    r.pushMatrix();
                    r.rotate((float)b * 15.0f);
                    r.triangle(-0.07f * S, 0, 0, -bl, 0.07f * S, 0);
                    r.popMatrix();
                }
            }
            r.popMatrix();
        }
    }
    r.end();
}

// Cinematic depth vignette + near-ground grade: soft black gradient strips down
// the four screen edges, with the BOTTOM darkest so the nearest ground reads as
// closer and richer. Frames the whole scene and pushes the center forward. Pure
// screen space, drawn last over everything.
static void drawVignette() {
    Renderer2D& r = app.renderer;
    r.beginScreenSpace();
    auto strip = [&](Vec2 e0, Vec2 e1, Vec2 i1, Vec2 i0, float a) {
        Renderer2D::Color edge{0, 0, 0, a}, in{0, 0, 0, 0};
        r.triangleV(e0, edge, e1, edge, i1, in);
        r.triangleV(e0, edge, i1, in, i0, in);
    };
    // bottom (darkest — nearest ground), top, left, right
    strip(Vec2(-1, -1), Vec2(1, -1), Vec2(1, -0.42f), Vec2(-1, -0.42f), 0.46f);
    strip(Vec2(-1, 1),  Vec2(1, 1),  Vec2(1, 0.60f),  Vec2(-1, 0.60f),  0.20f);
    strip(Vec2(-1, -1), Vec2(-1, 1), Vec2(-0.62f, 1), Vec2(-0.62f, -1), 0.24f);
    strip(Vec2(1, -1),  Vec2(1, 1),  Vec2(0.62f, 1),  Vec2(0.62f, -1),  0.24f);
    r.endScreenSpace();
}

// Drifting dust/spore motes at parallax depth planes — the cheap volume cue.
// Each mote's screen position is its base clip pos shifted by the camera's world
// position (scaled by the mote's depth, so near planes slide more) plus a slow
// sway, wrapped to stay on screen. Soft additive specks, faded out toward space.
static void drawMotes(float time) {
    float strength = 1.0f - spaceOverlay();
    if (strength <= 0.01f) return;
    Vec2 focus = app.spacewalking ? app.astronaut.pos : app.lander.pos;
    float aspect = (float)app.width / (float)app.height;
    auto wrap = [](float v) {                 // into [-1,1]
        v = fmodf(v + 1.0f, 2.0f);
        if (v < 0) v += 2.0f;
        return v - 1.0f;
    };
    Renderer2D& r = app.renderer;
    r.beginScreenSpace();
    r.setBlend(Renderer2D::Blend::Additive);
    for (const App::Mote& m : app.motes) {
        float ox = -focus.x * 0.00006f * m.depth + sinf(time * 0.08f + m.phase) * 0.03f;
        float oy = -focus.y * 0.00006f * m.depth + cosf(time * 0.07f + m.phase) * 0.03f;
        float px = wrap(m.x + ox);
        float py = wrap(m.y + oy);
        float a = (0.05f + 0.16f * m.depth) * strength;
        float hx = m.size / aspect, hy = m.size;
        r.fill(0.80f, 0.86f, 0.92f, a);
        r.quad(px - hx, py - hy, px + hx, py - hy, px + hx, py + hy, px - hx, py + hy);
    }
    r.setBlend(Renderer2D::Blend::Normal);
    r.endScreenSpace();
}

static constexpr float SHOOT_TRAIL_LIFE = 0.45f;  // seconds a trail point lingers

// Advance the lone shooting star. Only appears while the player is on foot in an
// atmosphere (a meteor burning up in the air, seen from the ground). A small
// glowing point darts ~50-100px and leaves a short trail that fades behind it.
static void updateShootingStar(float dt) {
    static unsigned int s = 1234567u;
    auto frand = [&]() { s = s * 1664525u + 1013904223u; return (s >> 8) * (1.0f / 16777216.0f); };
    App::ShootingStar& ss = app.shootingStar;

    for (auto& tp : ss.trail) tp.age += dt;
    while (!ss.trail.empty() && ss.trail.front().age > SHOOT_TRAIL_LIFE)
        ss.trail.erase(ss.trail.begin());

    // Gate: on foot, and the body you're standing on has air.
    bool canShow = app.spacewalking && dominantPlanet().atmosphere;
    if (!canShow) { ss.active = false; return; }          // leftover trail still fades

    if (!ss.active) {
        ss.timer -= dt;
        if (ss.timer > 0.0f) return;
        ss.active = true;
        ss.life = 0.0f;
        ss.maxLife = 0.28f + frand() * 0.22f;             // fast dart
        ss.trail.clear();
        ss.spawnFocus = app.astronaut.pos;
        float travelPx = 90.0f + frand() * 30.0f;          // ~100 px of travel
        float ang = 200.0f + frand() * 140.0f;             // down-ish diagonal
        Vec2 dirU(cosd(ang), sind(ang));
        // Pixel travel -> clip displacement (clip spans the screen as -1..1).
        Vec2 travelClip(dirU.x * travelPx * 2.0f / app.width,
                        dirU.y * travelPx * 2.0f / app.height);
        ss.head = Vec2(-0.6f + frand() * 1.2f, 0.1f + frand() * 0.7f);
        ss.vel = Vec2(travelClip.x / ss.maxLife, travelClip.y / ss.maxLife);
    } else {
        ss.life += dt;
        ss.head += ss.vel * dt;
        if (ss.trail.empty() ||
            (ss.head - ss.trail.back().p).length() > 0.006f)
            ss.trail.push_back({ss.head, 0.0f});
        if (ss.life >= ss.maxLife) {
            ss.active = false;
            ss.timer = 25.0f + frand() * 35.0f;           // next one in 25-60s
        }
    }
}

// Draw the shooting star: a bright moving point plus a streak made of its recent
// positions, each fading with age (newest brightest). Lives on a far background
// plane that drifts slightly with the ship, and is additive against the sky.
static void drawShootingStar() {
    App::ShootingStar& ss = app.shootingStar;
    if (ss.trail.size() < 2 && !ss.active) return;
    if (ss.trail.empty()) return;

    // Subtle parallax: the streak drifts opposite the ship's motion since spawn.
    Vec2 focus = app.spacewalking ? app.astronaut.pos : app.lander.pos;
    const float K = 0.00004f;
    Vec2 off(-(focus.x - ss.spawnFocus.x) * K, -(focus.y - ss.spawnFocus.y) * K);
    float aspect = (float)app.width / (float)app.height;

    Renderer2D& r = app.renderer;
    r.beginScreenSpace();
    r.setBlend(Renderer2D::Blend::Additive);
    auto alphaOf = [](float age) {
        float a = 1.0f - age / SHOOT_TRAIL_LIFE;
        return a < 0 ? 0 : a;
    };
    for (size_t i = 1; i < ss.trail.size(); i++) {
        const auto& p0 = ss.trail[i - 1];
        const auto& p1 = ss.trail[i];
        float a0 = alphaOf(p0.age), a1 = alphaOf(p1.age);
        if (a0 <= 0.003f && a1 <= 0.003f) continue;
        Vec2 q0(p0.p.x + off.x, p0.p.y + off.y);
        Vec2 q1(p1.p.x + off.x, p1.p.y + off.y);
        Vec2 dir(q1.x - q0.x, q1.y - q0.y);
        float dl = sqrtf(dir.x * dir.x + dir.y * dir.y);
        if (dl < 1e-5f) continue;
        dir.x /= dl; dir.y /= dl;
        // thin trail, width tapering with brightness so the tail thins out
        float w0 = 0.0022f * a0, w1 = 0.0022f * a1;
        Vec2 n0(-dir.y * w0 / aspect, dir.x * w0);
        Vec2 n1(-dir.y * w1 / aspect, dir.x * w1);
        Renderer2D::Color c0{0.78f, 0.86f, 1.0f, 0.7f * a0};
        Renderer2D::Color c1{0.78f, 0.86f, 1.0f, 0.7f * a1};
        Vec2 v0(q0.x + n0.x, q0.y + n0.y), v1(q0.x - n0.x, q0.y - n0.y);
        Vec2 v2(q1.x + n1.x, q1.y + n1.y), v3(q1.x - n1.x, q1.y - n1.y);
        r.triangleV(v0, c0, v1, c0, v2, c1);
        r.triangleV(v1, c0, v3, c1, v2, c1);
    }
    // The point of light itself: stacked additive halos from a wide soft glow
    // down to a hot white core, so it reads as a bright spark, not a dot.
    if (ss.active) {
        Vec2 h(ss.head.x + off.x, ss.head.y + off.y);
        // Ignite: grow from a small dim point up to full size/brightness over
        // the first ~70% of life, then burn out sharply at the end.
        float t = ss.life / ss.maxLife;
        float grow = smoothstepf(0.0f, 0.70f, t);
        float burn = 1.0f - smoothstepf(0.78f, 1.0f, t);
        float env = grow * burn;                  // brightness envelope
        float sizeF = 0.3f + 0.7f * env;          // 30% size -> full
        auto glow = [&](float px, float cr, float cg, float cb, float ca) {
            r.fill(cr, cg, cb, ca * env);
            r.ellipse(h.x, h.y, px * sizeF * 2.0f / app.width,
                      px * sizeF * 2.0f / app.height, 18);
        };
        glow(12.0f, 0.45f, 0.66f, 1.0f, 0.10f);   // wide cool halo
        glow(6.5f,  0.68f, 0.83f, 1.0f, 0.20f);
        glow(3.5f,  0.88f, 0.94f, 1.0f, 0.45f);
        glow(1.8f,  1.0f,  1.0f,  1.0f, 0.85f);   // hot core
    }
    r.setBlend(Renderer2D::Blend::Normal);
    r.endScreenSpace();
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
    gDebug.sunOverride      = EM_ASM_DOUBLE({ return window.DBG.sunOverride !== undefined ? window.DBG.sunOverride : -5; });

    float dens = EM_ASM_DOUBLE({ return window.DBG.planetDensity; });
    if (dens != gDebug.planetDensity) {
        gDebug.planetDensity = dens;
        for (Planet& p : app.planets) p.setDensity(dens);
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

    // E — exit ship (when landed) or re-enter (when close enough on foot).
    bool exitKey     = Input::isKeyDown(Input::KEY_E);
    bool exitPressed = exitKey && !app.prevExitKey;
    app.prevExitKey  = exitKey;

    if (exitPressed) {
        if (app.spacewalking) {
            float enterDist = dist(app.astronaut.pos.x, app.astronaut.pos.y,
                                   app.lander.pos.x,   app.lander.pos.y);
            if (enterDist < app.lander.radius * 4.0f) {
                app.astronaut.active = false;
                app.spacewalking     = false;
            }
        } else if (app.state == LANDED) {
            const Planet& p = dominantPlanet();
            float ang   = atan2d(app.lander.pos.y - p.center.y,
                                 app.lander.pos.x - p.center.x);
            float surfR = p.surfaceRadius(ang);
            Vec2 spawnPos(p.center.x + cosd(ang) * (surfR + Astronaut::RADIUS + 2.0f),
                          p.center.y + sind(ang) * (surfR + Astronaut::RADIUS + 2.0f));
            app.astronaut.spawn(spawnPos);
            app.spacewalking = true;
        }
    }

    // Advance the solar system, then re-pick the body the ship answers to.
    // While landed/spacewalking the dominant body is frozen: you're ON it, and
    // letting it flip (e.g. tidal tug-of-war on a moon) would teleport the
    // stuck-to-surface ship.
    for (Planet& p : app.planets) p.updateOrbit(app.planets, timeScale);
    // Tuning aid: pin the sun's day-cycle angle from the debug panel (-5 = auto).
    if (gDebug.sunOverride >= 0.0f) {
        Planet& sun = app.planets[app.sunIdx];
        sun.orbitAngle = gDebug.sunOverride;
        sun.updateOrbit(app.planets, 0.0f);
    }
    if (app.state == PLAYING && !app.spacewalking) {
        app.dominantIdx = dominantBody(app.lander.pos);
    }
    updateActiveField();

    switch (app.state) {
        case PLAYING:
            pollInput(timeScale);
            app.lander.update(app.planets, timeScale);
            updateContact();
            app.lander.updateBeam(dominantPlanet());
            runBeamGrab();
            for (Cow& cow : app.cows) {
                if (cow.update(homePlanet(), app.lander, app.cargo, MAX_CARGO, timeScale)) {
                    app.cargo += 1;
                    app.score += 50;
                }
            }
            break;
        case LANDED:
            // Stick to the body so the ship rides its orbit while parked.
            app.lander.pos = dominantPlanet().center + app.lander.landingOffset;
            updateContact();                 // keep altitude readout fresh
            if (!app.spacewalking && thrustPressed) {
                app.lander.liftOff(dominantPlanet());
                app.state = PLAYING;
            }
            break;
        case CRASHED:
            if (restartPressed) resetGame();
            break;
    }

    if (app.spacewalking) {
        // The astronaut simulates in the body's co-moving frame: carry them
        // with its orbital motion first, so their velocity stays relative to
        // the ground. Without this an orbiting moon slides out from under them
        // at orbital speed (~20 units/frame) — far faster than they can walk —
        // and ground friction (which damps toward zero world velocity) makes
        // catching up impossible. The planet already moved by orbitVel *
        // timeScale in updateOrbit above; this applies the same translation.
        app.astronaut.pos += dominantPlanet().orbitVel * timeScale;
        pollInputAstronaut(timeScale);
        app.astronaut.update(dominantPlanet(), timeScale);
    }

    updateZoom(timeScale);
    updateView(timeScale);
    updateShootingStar(dt);

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

    // 1a) The sun: additive corona + core at its true world position, behind
    //     the planets so they occlude it when they pass in front.
    drawSun(viewMat);

    // 1c) Sky band (atmosphere mode 0): the open sky above the terrain line.
    drawAtmospherePass(shaderTime, 0);

    // 1c1) Looming gas giant high in the sky, behind clouds and ridges.
    drawLoomingBody(shaderTime);

    // 1c2) Far hazy cloud band high in the sky, behind the ridges.
    drawFarClouds(viewMat, shaderTime);

    // 1c3) Occasional shooting star streaking across the background sky.
    drawShootingStar();

    // 1d) Distant ridge silhouettes, painted over the sky. Full unclipped
    //     bands — the terrain body drawn next occludes everything below the
    //     horizon, which is what makes them read as ranges BEYOND it.
    drawRidges(viewMat, false);

    // 1e) Volumetric light shafts raking through the sky; the body (next) clips
    //     their lower ends so they read as landing on the ground.
    drawLightShafts(viewMat, shaderTime);

    // 2) Planet bodies: SDF fill + material + slope lighting + crosshatch +
    //    day/night terminator, one fullscreen pass over every body (near
    //    planet from the live density field, far ones from silhouette rows).
    drawBodyPass();

    // 2a) Long cast shadows from peaks, darkening the valleys behind them.
    drawTerrainShadows(viewMat);

    // 2b) Ground shadows + cows, on top of the shaded body.
    app.renderer.begin(viewMat, app.width, app.height);
    drawEntityShadows();
    for (const Cow& cow : app.cows) cow.draw(app.renderer, homePlanet());
    app.renderer.end();

    // 2c) Ground tufts: small surface clutter pinned to the terrain, drawn over
    //     the body so the ground reads as detailed, not bare.
    drawProps(viewMat, shaderTime, false);

    // 3) Face fill (atmosphere mode 1): from space, the sky sphere tints the
    //    planet face; near the surface it's gated to zero so the ground stays
    //    clear.
    drawAtmospherePass(shaderTime, 1);

    // 3c) Ship + astronaut + space aids stay crisp on top of the sky.
    app.renderer.begin(viewMat, app.width, app.height);
    if (spaceAids) drawTrajectory();
    app.lander.drawBeam(app.renderer);
    if (app.spacewalking) app.astronaut.draw(app.renderer, dominantPlanet());
    app.lander.draw(app.renderer);   // always draw the actual ship
    if (spaceAids) {
        // In space the ship is sub-pixel, so overlay a locator beacon on top of it.
        drawShipBeacon();
        if (steering) drawHeadingArrow();
    }
    app.renderer.end();

    // 3d) Foreground framing ridges: the near dark plane, drawn AFTER the ship
    //     so it genuinely passes IN FRONT of the player (the closest depth
    //     plane), not behind. This is what sells the foreground as near-camera.
    drawRidges(viewMat, true);
    // ...plus the foreground props (spires/fronds) standing on that plane.
    drawProps(viewMat, shaderTime, true);

    // 4) Clouds (alpha) in front of everything, incl. the ship (fly behind them).
    drawCloudsPass(shaderTime);

    // 5) Drifting dust motes (atmospheric volume), then the framing vignette —
    //    the topmost screen-space layers.
    drawMotes(shaderTime);
    drawVignette();

    // Push HUD state to the DOM overlay (defined in shell.html).
    EM_ASM({
        if (window.updateHUD) window.updateHUD($0, $1, $2, $3, $4, $5, $6, $7, $8);
    }, app.lander.fuel, gDebug.maxFuel, app.lander.altitude,
       app.lander.vel.length(), (int)app.state, app.cargo, MAX_CARGO, app.score,
       (int)app.spacewalking);
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
