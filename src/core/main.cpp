#include <emscripten.h>
#include <emscripten/html5.h>
#include <GLES3/gl3.h>
#include <algorithm>
#include <cstdio>
#include <cmath>
#include <string>
#include <vector>

#include "../math/vecmath.h"
#include "../math/noise.h"
#include "../graphics/renderer2d.h"
#include "../world/planet.h"
#include "../world/system_gen.h"
#include "../world/nearfield.h"
#include "../world/cavefield.h"
#include "../world/deposits.h"
#include "../world/outposts.h"
#include "trade.h"
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

// Day -> dusk -> night, shared by the body and cloud shaders (and mirrored on
// the CPU by groundLight() below for everything drawn through Renderer2D).
//
// The light on the ground is a smooth function of the sun's elevation there
// (s = sine of the elevation: 1 overhead, 0 on the horizon, negative below):
//   - direct sunlight, eased in over the twilight width and colored by how
//     much air it has crossed — white through the day, gold, then orange-red
//     as the sun reaches the horizon (the same extinction that tints the sky);
//   - a twilight ambient that takes over as the sun goes under: sky-scattered
//     afterglow, rose on a blue-sky world, whatever the planet's air makes it
//     elsewhere;
//   - a dim night ambient in the sky's own tint.
// So the terminator is a long graded sweep — golden hour, dusk, blue hour —
// not a line. Airless worlds get a narrow width, white light and no twilight:
// a hard edge, as it should be with no air to scatter anything.
#define LIGHT_GLSL R"(
uniform vec3 u_extK[MAX_PLANETS];     // extinction (scatter + absorption) x sunset strength
uniform vec3 u_twiCol[MAX_PLANETS];   // twilight ambient color
uniform vec3 u_nightAmb[MAX_PLANETS]; // night ambient color
uniform float u_twiWidth[MAX_PLANETS];// twilight half-width in sunDot units

float sunDirect(int i, float s) {
  float w = u_twiWidth[i];
  return smoothstep(-0.33 * w, w, s);
}

vec3 groundLight(int i, float s) {
  float w = u_twiWidth[i];
  float airmass = 1.0 / max(s * 0.9 + 0.1, 0.05);
  vec3 sunCol = exp(-u_extK[i] * max(airmass - 2.0, 0.0));
  float twi = smoothstep(-1.6 * w, -0.2 * w, s) * (1.0 - smoothstep(-0.17 * w, 0.8 * w, s));
  float night = 1.0 - smoothstep(-1.6 * w, 0.0, s);
  return sunCol * sunDirect(i, s) + u_twiCol[i] * twi + u_nightAmb[i] * night;
}
)"

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
uniform sampler2D u_terrainTex; // terrain radius per angle (one row per planet)
// Per-planet sky identity (PlanetLook): scatter colors, brightness, and the
// pale ground haze. The day color comes from u_betaR; the sunset hue falls out
// of the same numbers through the extinction term.
uniform vec3 u_betaR[MAX_PLANETS];
uniform vec3 u_betaM[MAX_PLANETS];
uniform float u_skyGain[MAX_PLANETS];
uniform vec3 u_hazeCol[MAX_PLANETS];
uniform float u_horizonHaze;    // strength of the aerial haze band at the horizon (0 = off)
uniform int u_hazeIdx;          // planet the camera is at (its haze is fitted to the view)
uniform float u_hazeRef;        // radius the haze sits on for that planet (the ground under the player)
uniform float u_hazeView;       // haze scale height for that planet, world units
out vec4 fragColor;
)" LIGHT_GLSL R"(

// Terrain (surface) radius at a given outward direction, sampled from the
// silhouette LUT with linear wrap so the sky meets the ground exactly.
float terrainRadius(vec2 outward, int row) {
  float ang = degrees(atan(outward.y, outward.x));
  if (ang < 0.0) ang += 360.0;
  int n = textureSize(u_terrainTex, 0).x;
  float fidx = ang / 360.0 * float(n);
  int i0 = int(fidx) % n;
  int i1 = (i0 + 1) % n;
  float frac = fract(fidx);
  return mix(texelFetch(u_terrainTex, ivec2(i0, row), 0).r,
             texelFetch(u_terrainTex, ivec2(i1, row), 0).r, frac);
}

// Scattering coefficients (relative), adapted from Maxime Heckel's sky article.
// Rayleigh + Mie are per planet (u_betaR / u_betaM); the home world's values
// are the article's: (0.0058, 0.0135, 0.0331) blue, 0.008 achromatic haze.
const vec3 BETA_O = vec3(0.00065, 0.00188, 0.00008); // ozone absorption

// How far under the terrain outline the open-sky band keeps drawing (world
// units). See the gating in main().
const float SKY_UNDERLAP = 40.0;

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
    // The sky band (mode 0) runs a little way UNDER that line. The ground is
    // drawn afterwards, from the density grid, and covers it wherever there
    // is ground; the outline table only follows the grid to within a unit or
    // so, and a sky that stopped dead on the table would leave a sliver of
    // nothing — black, stars showing — wherever the table sits above the
    // ground actually drawn. The overlap is sky in every respect (haze, glow).
    float surfaceR = terrainRadius(outward, i);
    bool belowSurface = dist < surfaceR - (u_mode == 0 ? SKY_UNDERLAP : 0.0);
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
    vec3 betaR = u_betaR[i];
    vec3 betaM = u_betaM[i];
    float airmass = 1.0 / max(sunDot * 0.9 + 0.1, 0.05);
    vec3 extBeta = betaR + betaM + BETA_O;
    // Light reaching the low air has crossed more of it than light reaching
    // the top of the shell, so it is reddened more: at a low sun the horizon
    // goes orange while the sky above it is still blue. (No effect at noon,
    // where the path term is zero.)
    float path = max(airmass - 1.0, 0.0) * mix(1.35, 0.45, h);
    vec3 sunlight = exp(-extBeta * path * u_extinction);

    // Twilight: sky-scattered afterglow once the sun is at or under the
    // horizon — warm against the ground, cooling to the sky's own color
    // higher up. It outlasts the direct light by a long way, which is what
    // makes dusk a slow fade instead of a switch.
    float tw = u_twiWidth[i];
    float twi = smoothstep(-1.6 * tw, -0.1 * tw, sunDot) * (1.0 - smoothstep(-0.05, 0.9 * tw, sunDot));
    vec3 skyTint = betaR / max(max(betaR.r, betaR.g), max(betaR.b, 0.0001));
    // The warm end of the glow is the color of the sun itself seen through
    // the whole atmosphere (orange-red on a blue-sky world) while it is at
    // the horizon, sliding to the rose of the scattered afterglow as it sinks.
    vec3 sunLow = exp(-u_extK[i] * 11.7) * 1.7;
    vec3 warm = mix(u_twiCol[i], sunLow, smoothstep(-0.8 * tw, 0.15 * tw, sunDot));
    vec3 twiSky = mix(warm, skyTint * 0.30 + vec3(0.05, 0.02, 0.10), smoothstep(0.0, 0.30, h));

    // Sun glint + warm sunset band are limb-only (faded out toward the disk
    // centre by limbW) so they never smear a streak across the flat planet face.
    // They belong to the open-sky shell ABOVE the terrain only: over the planet
    // face (incl. mountain tops, which have a small nonzero h) we force limbW to 0
    // so the bright sunset band can't light up the peaks like snow at dusk.
    float limbW = smoothstep(0.05, 0.45, h);
    if (belowSurface) limbW = 0.0;
    float mieGlow = 1.0 + 2.0 * pow(max(sunDot, 0.0), 16.0) * limbW;
    float termBand = exp(-pow(sunDot * 2.0, 2.0) * 4.0) * limbW;
    vec3 rayleigh = betaR * densR;
    vec3 mie = betaM * densM * (mieGlow * 0.4 + termBand * 1.6 + 0.25);

    vec3 col = u_sunIntensity * u_skyGain[i] * sunlight * (rayleigh + mie) * dayNight;
    float opacity = dayNight * (densR * 0.8 + densM * 0.6) * 1.8;
    float airy = clamp(densR * 0.55 + densM * 0.5, 0.0, 1.0);
    col += twiSky * twi * 0.60 * airy;
    opacity = max(opacity, twi * 0.82 * airy);

    // Aerial haze at the horizon: a pale, luminous band in the lowest slice of
    // the air, so the sky lightens toward the land and the far ranges sit in
    // glow instead of against flat color. It takes the sunlight's extinction,
    // so it warms at dusk. Sky band only, and faded out on the climb to space
    // (the face fill below the terrain line has no matching term).
    if (!belowSurface) {
      // Physically the band is ~8% of the shell deep. For the world the
      // camera is at, it is also fitted to the view: zoomed in close, a band
      // that deep would cover the whole screen and wash the sky flat, so it
      // shrinks to stay a glow along the horizon and sits on the ground under
      // the player rather than on the planet's base radius.
      float hazeH = max((atmoR - baseR) * 0.085, 1.0);
      float hazeRef = baseR;
      if (i == u_hazeIdx) {
        float fit = clamp(u_hazeView / hazeH, 0.0, 1.0);
        hazeRef = mix(u_hazeRef, baseR, smoothstep(0.5, 1.0, fit));
        hazeH = max(min(hazeH, u_hazeView), 1.0);
      }
      float gh = exp(-max(dist - hazeRef, 0.0) / hazeH) * u_horizonHaze * (1.0 - u_overlay);
      // By day the band is the pale haze color in the sun's light; through
      // twilight it carries the afterglow, so the horizon keeps glowing after
      // the sun has gone.
      vec3 hazeLit = u_hazeCol[i] * 1.2 * (sunlight * 0.9 + 0.1) * dayNight + warm * twi * 0.95;
      col = mix(col, hazeLit, gh * 0.8);
      opacity = mix(opacity, max(opacity, max(dayNight, twi * 0.9)), gh * 0.8);
    }

    // Night side: a faint, almost-transparent tint of the sky color so the
    // shell stays visible against space instead of going fully black.
    float night = 1.0 - dayNight;
    col += betaR * 4.2 * densR * night;
    opacity += night * clamp(densR, 0.0, 1.0) * 0.22;
    opacity = clamp(opacity, 0.0, 1.0);

    // Soft top: the shell thins out over its last stretch instead of ending
    // on a hard edge, so from altitude the limb is a glow that falls off into
    // space rather than a cut-out disc.
    float topFade = 1.0 - smoothstep(0.84, 1.0, h);
    col *= topFade;
    opacity *= topFade;

    // Below the terrain line = planet face: only fill it with sky once you've
    // climbed out toward space, so the ground stays visible from up close.
    // (`belowSurface`, not the bare line: the sky band's overlap under the
    // line is sky, and has to be drawn at full strength to close the gap.)
    if (belowSurface) {
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
uniform sampler2D u_terrainTex; // terrain radius per angle (one row per planet)
// Per-planet cloud identity (PlanetLook): coverage multiplier + lit/shaded color.
uniform float u_cover[MAX_PLANETS];
uniform vec3 u_cloudLit[MAX_PLANETS];
uniform vec3 u_cloudDark[MAX_PLANETS];
out vec4 fragColor;
)" LIGHT_GLSL R"(

const float CLOUD_DRIFT = 0.004;

// Terrain radius at a given outward direction (matches the atmosphere shader), so
// the cloud deck meets the planet silhouette exactly instead of a smaller circle.
float terrainRadius(vec2 outward, int row) {
  float ang = degrees(atan(outward.y, outward.x));
  if (ang < 0.0) ang += 360.0;
  int n = textureSize(u_terrainTex, 0).x;
  float fidx = ang / 360.0 * float(n);
  int i0 = int(fidx) % n;
  int i1 = (i0 + 1) % n;
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
    float localCoverage = clamp(u_coverage * u_cover[i] * (0.25 + 1.6 * weather), 0.0, 1.0);
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
    vec3 cloudColor = mix(u_cloudDark[i], u_cloudLit[i], lit);
    // Same graded light as the ground, but clouds stand high: the sun is
    // still on them a little after it has left the land below, so they are
    // the last things to glow at dusk and the first at dawn.
    cloudColor *= groundLight(i, sunDot + 0.10);

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
uniform vec3 u_tone[MAX_PLANETS * 5];      // five flat terrain tones per planet (basin..cap)
uniform int u_activeField;      // planet index using the density texture (-1 = none)
uniform vec2 u_sunPos;
uniform float u_nightDarkness;  // brightness floor of the deepest night side
uniform float u_softness;       // terminator half-width in sunDot units
uniform sampler2D u_terrainTex; // terrain radius per angle (one row per planet)
uniform sampler2D u_fieldTex;   // R16F corner density grid (LINEAR-filtered)
uniform sampler2D u_carveTex;   // R16F: signed distance to dug space, in a window round the player
uniform vec2 u_carveOrigin;     // world position of its sample (0,0)
uniform float u_carveCell;      // world units between samples
uniform float u_carveSize;      // samples per side; 0 = nothing dug here
uniform float u_walkDrop;       // dug space is drawn this far below where it physically is
uniform float u_mouthRise;      // how far the far rim of a hole's mouth arches up at its middle
uniform float u_mouthRound;     // how far in from a hole's wall its rims take to round off
// Front-ground veil (second run of this pass, after the player is drawn).
uniform int u_veilPass;         // 1 = draw only the veil; 2 = cave mask (see drawCaveMask)
uniform float u_veilTop;        // the front strip of ground, as world units below
uniform float u_veilBottom;     //   the terrain line: where it starts (at the middle
                                //   of a hole's near rim) and where it ends
uniform float u_veilAlpha;      // how see-through it gets around a player who is down a hole
uniform vec2 u_playerPos;       // where the player is
uniform float u_playerIn;       // 0 = on the surface, 1 = down a hole
// Burn: the last few cutter bites, still hot. xy = where (true position),
// z = heat 1 -> 0 as it cools, w = radius.
#define MAX_BURNS 12
uniform vec4 u_burn[MAX_BURNS];
uniform int u_burnCount;
// Cave lights: the glowing crystals and mushrooms nearest the camera, and the
// player's own lamp. xy = where (true position), z = radius; and its color.
#define MAX_GLOW 14
uniform vec4 u_glow[MAX_GLOW];
uniform vec3 u_glowCol[MAX_GLOW];
uniform int u_glowCount;
// Natural cave mouths near the view: x = angle (degrees), y = half-width
// (degrees). Their rims are uneven; a dug hole's are clean.
#define MAX_MOUTHS 8
uniform vec2 u_natMouth[MAX_MOUTHS];
uniform int u_natMouthCount;
// A hole is the ground it was cut in, a shade darker the deeper it goes:
// darkest (u_holeFloor x the ground) from u_holeDepth down, never black.
uniform float u_holeFloor;
uniform float u_holeDepth;
uniform vec2 u_fieldOrigin;     // world position of corner (0,0)
uniform float u_fieldCell;      // world units per grid cell
uniform vec2 u_fieldSize;       // corner counts (w,h)
out vec4 fragColor;
)" LIGHT_GLSL R"(

float terrainRadius(vec2 outward, int row) {
  float ang = degrees(atan(outward.y, outward.x));
  if (ang < 0.0) ang += 360.0;
  int n = textureSize(u_terrainTex, 0).x;
  float fidx = ang / 360.0 * float(n);
  int i0 = int(fidx) % n;
  int i1 = (i0 + 1) % n;
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
// Signed distance to what has been DUG at a world point (CarveMap): negative
// inside a hole. A large positive number where nothing has been dug, or
// outside the window.
float carveDist(vec2 world) {
  if (u_carveSize < 1.0) return 1.0e4;
  vec2 g = (world - u_carveOrigin) / u_carveCell;
  if (g.x < 0.0 || g.y < 0.0 || g.x > u_carveSize - 1.0 || g.y > u_carveSize - 1.0) return 1.0e4;
  return texture(u_carveTex, (g + 0.5) / u_carveSize).r;
}

// How much of the ground's light is left at a depth into a hole: 0.88 for a
// scratch (still a step darker than the ground, so it reads as a hole), easing
// down to u_holeFloor at u_holeDepth and flat below that. Ease-out, so most of
// the darkening happens in the first stretch: the first few bites show.
float holeShade(float depth) {
  float x = clamp(depth / max(u_holeDepth, 1.0), 0.0, 1.0);
  float k = (1.0 - x) * (1.0 - x);
  return mix(u_holeFloor, 0.88, k);
}

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
    // The terrain as generated: anti-aliased rock coverage across its 0 line.
    float terrain = planetDensity(i, world);
    float terrainRock = clamp(terrain / aaw + 0.5, 0.0, 1.0);

    // Dug space. The ground is drawn as seen from above at an angle: the
    // terrain line is the FAR edge of the ground, and the walking line — where
    // the player stands and digs — shows u_walkDrop below it (that is why the
    // astronaut's boots are drawn below the terrain line). So a hole is drawn
    // that same distance below where it physically is: its mouth opens at the
    // walking line with a strip of ground still behind it, and its floor is
    // where the boots of someone standing in it are drawn. This pixel shows a
    // hole if the true point it stands for (P) was rock and has been cut.
    float dug = 0.0;
    float dugCover = 0.0;      // dug, a pixel wider: what the front ground has to cover
    float cutEdge = 1.0e4;     // at P: distance into the rock from the nearest cut
    float mouth = 0.0;         // 0 at a hole's wall at ground level .. 1 toward its middle
    float riseK = 1.0;         // a natural mouth's rims wander: how far the far rim
    float sagK = 1.0;          //   rises / the near rim sags here, against the clean ellipse
    float under = surfaceR - dist;             // how far below the terrain line
    vec2 P = world + outward * u_walkDrop;
    float rise = 0.0;          // how far the far rim lifts this pixel's P (see below)
    float nat = 0.0;           // 1 = this column is a natural cave's mouth
    float holeH = 0.0;         // how deep the hole under this column goes (mouth columns only)
    if (i == u_activeField && u_carveSize > 0.5) {
      // The mouth. A round hole in the ground, seen from a low angle, is an
      // ellipse lying on the walking line: its far rim arches up behind the
      // middle of the hole, its near rim sags below it, and the two meet at
      // the hole's sides. `inside` is how far this column is from the hole's
      // wall at ground level (0 outside any hole that breaks the surface);
      // `mouth` turns that into a quarter-circle, so both rims leave the wall
      // rounded rather than at a corner. The far rim is made by easing the
      // drop off — near the top only, so floors further down stay exactly
      // where boots are drawn; the near rim is the edge of the front ground
      // (below).
      float inside = max(-carveDist(u_planetPos[i] + outward * (surfaceR - 2.0)), 0.0);
      float fromMiddle = 1.0 - clamp(inside / u_mouthRound, 0.0, 1.0);
      mouth = sqrt(max(1.0 - fromMiddle * fromMiddle, 0.0));
      // A natural cave's mouth is not a clean ellipse: its rims wander, in
      // lumps a few strides long (shape, not grain — flat fills either side).
      if (mouth > 0.0) {
        float angDeg = degrees(atan(outward.y, outward.x));
        for (int k = 0; k < MAX_MOUTHS; k++) {
          if (k >= u_natMouthCount) break;
          float da = abs(mod(angDeg - u_natMouth[k].x + 540.0, 360.0) - 180.0);
          if (da < u_natMouth[k].y) nat = 1.0;
        }
        float along = radians(angDeg) * u_baseRadius[i];
        riseK = mix(1.0, 0.45 + 0.90 * vnoise(vec2(along * 0.050, 7.3)), nat);
        sagK = mix(1.0, 0.35 + 1.30 * vnoise(vec2(along * 0.043, 31.9)), nat);
        // How deep the hole is under this column: march down from the terrain
        // line until the carve layer says rock, and put the floor where its
        // value crosses zero between the last two samples. That moves
        // smoothly from column to column (and as you cut), so the mouth's
        // shade has no seams. A natural mouth opens onto a tunnel that runs
        // sideways, which a straight-down look would call a trench: it
        // counts as deep.
        if (u_veilPass == 0) {
          if (nat > 0.5) {
            holeH = u_holeDepth;
          } else {
            float prevD = 2.0;
            float prevC = min(carveDist(u_planetPos[i] + outward * (surfaceR - prevD)), -0.001);
            holeH = prevD;
            for (int k = 1; k < 40; k++) {
              float dk = 2.0 + 12.0 * float(k);
              if (dk > u_holeDepth + 12.0) break;
              float ck = carveDist(u_planetPos[i] + outward * (surfaceR - dk));
              if (ck >= 0.0) { holeH = prevD + (dk - prevD) * prevC / (prevC - ck); break; }
              prevD = dk; prevC = ck; holeH = dk;
            }
            holeH = min(holeH, u_holeDepth);   // past that, deeper looks the same
          }
        }
      }
      rise = u_mouthRise * riseK * mouth * (1.0 - smoothstep(4.0, 26.0, under));
      P = world + outward * (u_walkDrop - rise);
      float tP = planetDensity(i, P);
      float cP = carveDist(P);
      float wasRock = clamp(tP / aaw + 0.5, 0.0, 1.0);
      // (Tested against the carve alone: "not rock" is also true along the
      // terrain's own anti-aliased edge, which would draw a hairline of hole
      // the whole length of the walking line.)
      dug = wasRock * (1.0 - clamp(cP / aaw + 0.5, 0.0, 1.0));
      // Solid wherever `dug` is anything at all, so ground laid back over a
      // hole leaves no dark thread along the hole's anti-aliased walls.
      dugCover = wasRock * (1.0 - clamp(cP / aaw - 0.5, 0.0, 1.0));
      if (tP > 0.0) cutEdge = cP;
    }
    float rock = terrainRock * (1.0 - dug);
    // Cave interior: open space that sits below the outer silhouette (a
    // natural pocket). Rendered opaque so stars never show through.
    float below = 1.0 - smoothstep(surfaceR - u_fieldCell, surfaceR, dist);
    // A hole you dug always shows the darker rock behind it, however shallow,
    // so it reads as a hole rather than a window onto the sky.
    float cave = max(below * (1.0 - terrainRock), dug);
    // How much the ground in front covers whatever has been dug at this
    // pixel (the veil pass, below, is what lays it over). It begins at the
    // hole's near rim — a curve: on the walking line where the hole's wall
    // is, sagging to u_veilTop at its middle.
    //  - Seen from the surface, it covers everything: a hole is its mouth
    //    and nothing else, the shaft under it is out of sight.
    //  - Down a hole, you get the cutaway: the front strip thins around you
    //    so you can see yourself behind it, and below the strip the rock is
    //    simply open.
    float cover = 0.0;
    float wallCover = 0.0;     // the same, for the rock beside a hole (see Burn)
    if (i == u_activeField) {
      float rim = mix(u_walkDrop, u_walkDrop + (u_veilTop - u_walkDrop) * sagK, mouth);
      float strip = 1.0 - smoothstep(u_veilBottom - 5.0, u_veilBottom + 5.0, under);
      float nearPlayer = 1.0 - smoothstep(90.0, 170.0, distance(world, u_playerPos));
      float stripCover = mix(1.0, u_veilAlpha, u_playerIn * nearPlayer);
      float covered = mix(1.0 - u_playerIn, stripCover, strip);
      cover = covered * smoothstep(rim - 1.0, rim + 1.0, under);
      // Rock beside a hole starts exactly at the rim, where `cover` is still
      // fading in; taken from just above it so no thread of scorch is left
      // showing along the walking line.
      wallCover = covered * smoothstep(rim - 3.0, rim - 1.0, under);
    }
    // The veil pass only cares about dug space the front ground covers;
    // everything else is the main pass's business.
    float veil = 0.0;
    // The cave mask: just the cave's open space, as drawn. No color, only a
    // stencil mark where the hole is solid (cave formations are clipped to
    // it, so the wall they grow out of hides their roots).
    if (u_veilPass == 2) {
      if (i != u_activeField || dug < 0.5) continue;
      fragColor = vec4(0.0);
      return;
    }
    if (u_veilPass == 1) {
      if (i != u_activeField) continue;
      veil = dugCover * cover;
      if (veil <= 0.0) continue;
    } else if (rock <= 0.0 && cave <= 0.0) continue;

    // Where the sun stands here (the light model below turns it into light).
    vec2 toSun = normalize(u_sunPos - u_planetPos[i]);
    float sunDot = dot(outward, toSun);

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
      float skin = 1.0 - smoothstep(0.0, u_fieldCell * 6.0, terrain);
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
    // The tones are per planet (PlanetLook::tones) — the archetype swaps the
    // palette, the banding rule stays the same on every world.
    // Band edges are anti-aliased against the pixel footprint (bw = about a
    // pixel and a half, in altitude-fraction units), so the boundaries stay
    // crisp flat-color shorelines up close and soften only when zoomed out.
    vec3 tBasin = u_tone[i * 5];
    float bw = clamp(aaw / max(u_noiseBand[i], 1.0), 0.0015, 0.02);
    vec3 col = mix(tBasin, u_tone[i * 5 + 1], smoothstep(0.18 - bw, 0.18 + bw, ragged));
    col = mix(col, u_tone[i * 5 + 2], smoothstep(0.44 - bw, 0.44 + bw, ragged));
    col = mix(col, u_tone[i * 5 + 3], smoothstep(0.62 - bw, 0.62 + bw, ragged));
    col = mix(col, u_tone[i * 5 + 4], smoothstep(0.78 - bw, 0.78 + bw, ragged));

    // Lighting. The day/night sweep is the smooth graded light above — the
    // tones stay flat fields, the light across them changes slowly. Slopes
    // turned well away from the sun drop one step, with a soft edge, and only
    // while there is direct sun to be turned away from.
    float flank = smoothstep(0.35, 0.75, -slopeLight);
    vec3 lightK = groundLight(i, sunDot) * (1.0 - 0.36 * flank * sunDirect(i, sunDot));
    col *= lightK;
    // The ground as it is here, before any burn or cave light: what a hole
    // cut here is a darker shade of.
    vec3 ground = col;

    // Burn. Rock right beside a cut is scorched — a dark burnt rim that stays
    // — and where the cutter has just been it is still glowing: the last few
    // bites heat the rock around them and cool over a second or two. The glow
    // is emitted, not lit, so it shows at night.
    float heat = 0.0;
    if (i == u_activeField && u_veilPass == 0) {
      for (int k = 0; k < MAX_BURNS; k++) {
        if (k >= u_burnCount) break;
        vec2 bd = P - u_burn[k].xy;
        heat += u_burn[k].z * u_burn[k].z * 0.55 * exp(-dot(bd, bd) / (u_burn[k].w * u_burn[k].w));
      }
      heat = min(heat, 1.0);
      // The walls of a hole are behind the front ground, so the scorch shows
      // only as far as that ground lets it (not at all from the surface). The
      // glow is light, and some of it comes through regardless.
      float scorch = (1.0 - smoothstep(0.0, 7.0, cutEdge)) * (1.0 - wallCover);
      col *= 1.0 - 0.45 * scorch;
      // Orange through the rim, a thin yellow line right on the cut face.
      float hotRim = 1.0 - smoothstep(0.0, 9.0, cutEdge);
      float hotLine = 1.0 - smoothstep(0.0, 2.5, cutEdge);
      col += (vec3(1.0, 0.34, 0.07) * heat * hotRim * 0.75 + vec3(1.0, 0.80, 0.40) * heat * hotLine * 0.55) *
             (1.0 - 0.6 * wallCover);
    }

    // Cave light: what the glowing things (and the player's lamp) throw. It
    // falls on the back wall of the cave (below) and catches the rock right
    // at the cave's edge. Emitted, so it is there at night too.
    vec3 glow = vec3(0.0);
    if (i == u_activeField && u_veilPass == 0) {
      for (int k = 0; k < MAX_GLOW; k++) {
        if (k >= u_glowCount) break;
        vec2 gd = P - u_glow[k].xy;
        glow += u_glowCol[k] * exp(-dot(gd, gd) / (u_glow[k].z * u_glow[k].z));
      }
      // Many lights close together pool; they don't add up to white.
      glow /= 1.0 + 1.6 * dot(glow, vec3(0.34));
      col += glow * (1.0 - smoothstep(0.0, 12.0, cutEdge)) * (1.0 - wallCover) * 0.26;
    }

    // Front-ground veil. The ground is drawn as if seen from above at an
    // angle: the terrain line is its FAR edge, and the strip just below that
    // line is ground in front of the walking line — where the plants in front
    // of you stand. A shaft dug at the walking line is BEHIND that strip. So
    // where a hole passes behind it, the ground is drawn back over the hole
    // (and over whoever is in it), in its own color — `cover` says how
    // solidly: completely from the surface, see-through around a player who
    // is down there.
    if (u_veilPass == 1) {
      accumCol += col * veil * (1.0 - accumA);
      accumA += veil * (1.0 - accumA);
      continue;
    }

    // Cave interior: the ground it was cut in, a shade darker the deeper the
    // part of it you are looking at — so a scratch is barely darker than the
    // ground, a deep shaft or a cave levels off at u_holeFloor of it, and
    // nothing is ever black. Bands run straight through: a hole in snow is
    // darker snow.
    //  - In section (the cutaway, and natural pockets in the cross-section)
    //    that is how far below the terrain line the point shown physically is.
    //  - In a hole's mouth, seen from the surface, you see its far wall from
    //    the lip down: lit at the far rim, falling in ONE smooth fall to the
    //    depth of the hole under it at the near rim. So a dent stays light
    //    across its whole mouth and a deep shaft still reads deep.
    float farRim = u_walkDrop - u_mouthRise * riseK * mouth;
    float nearRim = mix(u_walkDrop, u_walkDrop + (u_veilTop - u_walkDrop) * sagK, mouth);
    float sectionDepth = max(under - (u_walkDrop - rise) * dug, 0.0);
    float wallT = clamp((under - farRim) / max(nearRim - farRim, 1.0), 0.0, 1.0);
    // The mouth view hands over to the section only once the front ground
    // covers it fully (nearRim + 1), so no lighter sliver shows at the rim.
    float inMouth = smoothstep(0.0, 0.35, mouth) * (1.0 - smoothstep(nearRim + 1.0, nearRim + 3.0, under)) *
                    (1.0 - u_playerIn);
    float depthShown = mix(sectionDepth, holeH * wallT, inMouth);
    vec3 caveCol = ground * holeShade(depthShown);
    caveCol += vec3(0.60, 0.20, 0.05) * heat * dug * 0.30;   // the hole is lit by its own hot walls
    caveCol += glow * dug * 0.55;                            // ...and by whatever glows in it

    float alpha = clamp(rock + cave, 0.0, 1.0);
    vec3 rgb = col * rock + caveCol * cave;
    accumCol += rgb * (1.0 - accumA);
    accumA += alpha * (1.0 - accumA);
  }
  if (u_veilPass == 2) discard;         // the mask marks open cave space only
  fragColor = vec4(accumCol, accumA);   // premultiplied "over"
}
)";

// ============================================================================
// Water (see docs/art-style.md). A world's water stands at one level, a circle
// round its center (Planet::seaRadius): wherever the ground dips below it there
// is a pond, a lake or a sea, and whatever has been dug that connects to one is
// flooded up to the same level. Drawn in two runs of this shader:
//
//   mode 0  the water itself, BEHIND the crust — a disc at the sea's radius,
//           laid down before the body pass, which then paints the rock over
//           it. So it shows exactly where the ground is lower than the water,
//           with the body pass's own anti-aliased ground line as its bed.
//           (The original game's "water disk behind the planet".)
//   mode 1  the water IN FRONT of whoever is in it — run after the ship and
//           the astronaut are drawn. The ground is seen from above at an
//           angle, with the walking line u_walkDrop below the terrain line
//           (see the body shader), and the water is drawn the same way: its
//           surface runs from its far edge (the level itself) down to where it
//           meets the walking line, u_walkDrop lower. That strip is the far
//           part of the surface, behind the walker; from there down the water
//           is in front of them, and tints whatever is in it. This run also
//           puts the water into flooded holes and tunnels.
//   mode 2  the water over the bed in front of the walking line — run after
//           the front-ground veil. The strip of ground just below the terrain
//           line is ground in front of the walker (where the nearest plants
//           stand on land); under a lake that strip is lake bed, seen through
//           the water standing on it. So it is tinted too, the more the deeper
//           the water there — nothing at the shore — and so is the front
//           ground the veil lays back over a hole dug in the bed.
//
// The style is the ground's: flat fields (far surface, shallows, deep water),
// lit by the hour's light, no grain. The only motion is a slow swell on the
// surface line and a few short glints drifting along the far strip.
// ============================================================================
static const char* kWaterFrag = R"(#version 300 es
precision highp float;
#define MAX_PLANETS 10
uniform vec2 u_resolution;
uniform vec2 u_viewFocus;
uniform vec2 u_screenCenter;
uniform float u_viewScale;
uniform float u_viewRotation;
uniform int u_planetCount;
uniform vec2 u_planetPos[MAX_PLANETS];
uniform float u_seaR[MAX_PLANETS];      // radius of the water's surface (0 = a dry world)
uniform vec3 u_water[MAX_PLANETS];      // the shallows
uniform vec3 u_waterDeep[MAX_PLANETS];  // deep water
uniform vec3 u_waterTop[MAX_PLANETS];   // the far surface: paler, toward the sky's haze
uniform vec2 u_sunPos;
uniform float u_time;
uniform int u_mode;             // 0 = the water, behind the crust; 1 = in front of the walking line; 2 = over the bed in front
uniform float u_stripBottom;    // how far below the terrain line the front strip of ground reaches
uniform int u_activeField;      // planet whose density grid / carve window are bound (-1 = none)
uniform sampler2D u_terrainTex; // terrain radius per angle (one row per planet)
uniform sampler2D u_fieldTex;   // R16F corner density grid
uniform sampler2D u_carveTex;   // R16F: signed distance to dug space
uniform sampler2D u_wetTex;     // R8: 1 where the water has got into dug space
uniform vec2 u_carveOrigin;
uniform float u_carveCell;
uniform float u_carveSize;      // samples per side; 0 = no carve window
uniform vec2 u_fieldOrigin;
uniform float u_fieldCell;
uniform vec2 u_fieldSize;
uniform float u_walkDrop;       // the walking line is drawn this far below the terrain line
uniform float u_tint;           // how strongly what is under water takes its color
out vec4 fragColor;
)" LIGHT_GLSL R"(

float terrainRadius(vec2 outward, int row) {
  float ang = degrees(atan(outward.y, outward.x));
  if (ang < 0.0) ang += 360.0;
  int n = textureSize(u_terrainTex, 0).x;
  float fidx = ang / 360.0 * float(n);
  int i0 = int(fidx) % n;
  int i1 = (i0 + 1) % n;
  float frac = fract(fidx);
  return mix(texelFetch(u_terrainTex, ivec2(i0, row), 0).r,
             texelFetch(u_terrainTex, ivec2(i1, row), 0).r, frac);
}

float fieldDensity(vec2 world) {
  vec2 uv = ((world - u_fieldOrigin) / u_fieldCell + 0.5) / u_fieldSize;
  return texture(u_fieldTex, uv).r;
}

// Signed distance to dug space, and whether the water has got into it there.
// Nothing dug (and nothing flooded) outside the carve window.
float carveDist(vec2 world) {
  if (u_carveSize < 1.0) return 1.0e4;
  vec2 g = (world - u_carveOrigin) / u_carveCell;
  if (g.x < 0.0 || g.y < 0.0 || g.x > u_carveSize - 1.0 || g.y > u_carveSize - 1.0) return 1.0e4;
  return texture(u_carveTex, (g + 0.5) / u_carveSize).r;
}
float wetMask(vec2 world) {
  if (u_carveSize < 1.0) return 0.0;
  vec2 g = (world - u_carveOrigin) / u_carveCell;
  if (g.x < 0.0 || g.y < 0.0 || g.x > u_carveSize - 1.0 || g.y > u_carveSize - 1.0) return 0.0;
  return texture(u_wetTex, (g + 0.5) / u_carveSize).r;
}

float hash1(float n) { return fract(sin(n * 12.9898) * 43758.5453); }

// The water's own color a given depth below its surface: three flat fields —
// the shallows, a middle tone from a couple of body heights down, and deep
// water well below that — the lines between them wandering a little. A pond
// is all shallows; only a sea shows all three.
vec3 waterTone(int i, float depth, float along, float aaw) {
  float wander = 9.0 * sin(along * 0.0110) + 6.0 * sin(along * 0.0270 + 1.7);
  float mid = smoothstep(-aaw, aaw, depth - (90.0 + wander));
  float deep = smoothstep(-aaw, aaw, depth - (215.0 - wander * 1.4));
  return mix(u_water[i], u_waterDeep[i], 0.5 * mid + 0.5 * deep);
}

// Short pale strokes lying on the far strip of the surface, drifting slowly and
// coming and going: the light catching the swell. Two rows of them.
float glints(float along, float depth, float t, float aaw) {
  float g = 0.0;
  for (int k = 0; k < 2; k++) {
    float row = k == 0 ? 4.0 : 8.6;
    float cell = k == 0 ? 61.0 : 89.0;
    float x = along + t * (k == 0 ? 2.1 : -1.5);
    float id = floor(x / cell);
    float h = hash1(id + float(k) * 17.0);
    float len = 6.0 + 11.0 * hash1(id * 1.7 + 3.1);
    float c = (id + 0.25 + 0.5 * h) * cell;
    float inX = 1.0 - smoothstep(len * 0.5 - aaw, len * 0.5 + aaw, abs(x - c));
    float inY = 1.0 - smoothstep(0.55, 0.55 + aaw, abs(depth - row));
    float on = step(0.42, h) * smoothstep(-0.2, 0.6, sin(t * 0.33 + h * 40.0));
    g = max(g, inX * inY * on);
  }
  return g;
}

void main() {
  vec2 pixel = vec2(gl_FragCoord.x, u_resolution.y - gl_FragCoord.y);
  vec2 centered = pixel - u_screenCenter;
  float c = cos(-u_viewRotation), s = sin(-u_viewRotation);
  vec2 unrot = vec2(c * centered.x - s * centered.y, s * centered.x + c * centered.y);
  vec2 world = unrot / u_viewScale + u_viewFocus;
  float aaw = max(1.5 / u_viewScale, 0.0001);
  // The fine things (swell, glints, the line where the surface meets the
  // walking line) only exist up close; they melt away as the camera pulls out.
  float detail = 1.0 - smoothstep(1.2, 3.5, aaw);

  vec3 accumCol = vec3(0.0);
  float accumA = 0.0;
  for (int i = 0; i < MAX_PLANETS; i++) {
    if (i >= u_planetCount) break;
    float seaR = u_seaR[i];
    if (seaR <= 0.0) continue;
    if (u_mode != 0 && i != u_activeField) continue;
    vec2 d = world - u_planetPos[i];
    float dist = length(d);
    if (dist > seaR + 4.0 + aaw) continue;
    vec2 outward = d / max(dist, 0.0001);
    float along = atan(outward.y, outward.x) * seaR;
    // A slow swell on the surface: a unit or two, long and lazy.
    float swell = (1.1 * sin(along * 0.041 + u_time * 0.30) + 0.6 * sin(along * 0.097 - u_time * 0.47)) * detail;
    float top = seaR + swell;
    vec2 toSun = normalize(u_sunPos - u_planetPos[i]);
    vec3 light = groundLight(i, dot(outward, toSun));

    if (u_mode == 0) {
      float depth = top - dist;
      if (depth < -aaw) continue;
      // Well inside the rock the crust covers it anyway.
      if (dist < terrainRadius(outward, i) - 40.0 - 4.0 * aaw) continue;
      float cov = clamp(depth / aaw + 0.5, 0.0, 1.0);
      vec3 col = waterTone(i, depth, along, aaw);
      // The far strip of the surface, from its far edge to the walking line.
      float strip = 1.0 - smoothstep(u_walkDrop - aaw, u_walkDrop + aaw, depth);
      col = mix(col, u_waterTop[i], strip);
      col = mix(col, min(u_waterTop[i] * 1.22 + 0.05, vec3(1.0)), glints(along, depth, u_time, aaw) * detail * 0.85);
      col *= light;
      accumCol += col * cov * (1.0 - accumA);
      accumA += cov * (1.0 - accumA);
    } else if (u_mode == 2) {
      float surfaceR = terrainRadius(outward, i);
      float over = top - surfaceR;                // the water standing on the bed in this column
      float under = surfaceR - dist;              // how far below the terrain line this pixel is
      if (over <= 0.0 || under < u_walkDrop - 2.0 * aaw || under > u_stripBottom + 8.0) continue;
      // Only where the walking plane is ground (rock, or a hole in it): the
      // open water above the bed was laid over by mode 1.
      vec2 P = world + outward * u_walkDrop;
      float ground = clamp(fieldDensity(P) / aaw + 0.5, 0.0, 1.0);
      float strip = 1.0 - smoothstep(u_stripBottom - 7.0, u_stripBottom + 7.0, under);
      float a = u_tint * ground * strip * smoothstep(0.0, 28.0, over);
      if (a <= 0.0) continue;
      vec3 col = waterTone(i, over, along, aaw) * light;
      accumCol += col * a * (1.0 - accumA);
      accumA += a * (1.0 - accumA);
    } else {
      // The point of the walking plane shown at this pixel.
      vec2 P = world + outward * u_walkDrop;
      float depthP = top - (dist + u_walkDrop);
      if (depthP < -aaw) continue;
      float tP = fieldDensity(P);
      float cP = carveDist(P);
      // Open above the ground: the sea itself. Dug out of the rock: water only
      // where it has got in.
      float open = clamp(-tP / aaw + 0.5, 0.0, 1.0);
      float dug = clamp(tP / aaw + 0.5, 0.0, 1.0) * (1.0 - clamp(cP / aaw + 0.5, 0.0, 1.0));
      float flooded = dug * smoothstep(0.30, 0.60, wetMask(P));
      float inWater = clamp(open + flooded, 0.0, 1.0) * clamp(depthP / aaw + 0.5, 0.0, 1.0);
      if (inWater <= 0.0) continue;
      // In a flooded hole there is no water behind what you see, only the
      // hole's back wall: lay it on thicker there so it still reads as water,
      // and in ONE tone whatever the depth — a tone line across a narrow
      // shaft reads as a second water surface part-way down it.
      float inHole = flooded / max(open + flooded, 0.0001);
      vec3 col = mix(waterTone(i, depthP + u_walkDrop, along, aaw),
                     mix(u_water[i], u_waterDeep[i], 0.35), inHole);
      float a = u_tint * mix(1.0, 1.45, inHole);
      // Where the surface meets the walking line: a thin pale line, across
      // whatever stands in the water.
      float line = (1.0 - smoothstep(1.3, 1.3 + aaw, depthP)) * detail;
      col = mix(col, u_waterTop[i] * 1.10, line);
      a = mix(min(a, 0.92), 0.80, line) * inWater;
      col *= light;
      accumCol += col * a * (1.0 - accumA);
      accumA += a * (1.0 - accumA);
    }
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
    StarSystem system;              // seed + star name + indices of the generated system
    int sunIdx = 0;
    int homeIdx = 1;
    int dominantIdx = 1;
    int activeFieldIdx = -1;
    int stuckIdx = 1;               // body a landed / wrecked ship rides along with
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
    Lander::Encounter encounter;    // closest pass to another world on that path

    // Navigation overlay (orbit rings, body markers, DOM name labels): fades
    // in whenever the view is zoomed far enough out that worlds are specks.
    // M frames the whole system.
    bool mapMode = false;
    bool prevMapKey = false;
    float navAlpha = 0.0f;
    bool navLabelsShown = false;
    int navHudKey = -1;             // change detector for the DOM nav readout
    std::string labelBuf;

    // Sun-angle override bookkeeping: lets a pin being engaged / moved /
    // released carry the player along with the world instead of stranding them.
    int prevPinIdx = -1;
    float prevSunOverride = -5.0f;

    // Mining laser (on foot): what it is cutting, where the beam ends, and the
    // loose bits it throws off. Particles are stored relative to the center of
    // the world you're on, so they ride its orbit with everything else.
    struct Particle {
        Vec2 local, vel;
        float life, maxLife, size;
        float r, g, b;
        int mode;                   // 0 spark, 1 harvested bit flying to the player, 2 ember (falls), 3 smoke (rises, spreads),
                                    // 4 water droplet (falls), 5 bubble (rises to the surface), 6 ripple ring on the water
    };
    std::vector<Particle> particles;
    // The hand tool has two modes (Q switches): the mining laser, which cuts
    // plants / rocks / crystals, and the terrain cutter, which eats rock.
    int toolMode = 0;               // 0 = mining laser, 1 = terrain cutter
    // The terrain cutter is meant to be an upgrade you earn (natural caves are
    // the way underground until then). Unlocked from the start for now.
    bool cutterUnlocked = true;
    bool prevToolKey = false;
    bool laserCutting = false;      // this frame's beam is the cutter (for its color)
    float cutTimer = 0.0f;          // time banked toward the next bite
    int cutBites = 0;               // bites taken (paces the smoke)
    // The last few bites, still hot (planet-local, cooling) — the body shader
    // makes the rock around them glow.
    static constexpr int MAX_BURNS = 12;
    struct Burn { Vec2 local; float heat = 0.0f; float radius = 26.0f; };
    Burn burns[MAX_BURNS];
    int burnNext = 0;
    float inHole = 0.0f;            // eased 0..1: the player is down a dug hole
    // Water effects: was the astronaut in the water last frame (for the
    // splash going in), and clocks for their bubbles and ripples and the
    // floating ship's.
    bool wasWet = false;
    float bubbleIn = 0.0f, rippleIn = 0.0f, shipRippleIn = 0.0f;
    int floodedNow = 0;             // samples of dug space the water has just got into (since last looked at)
    bool laserOn = false;
    int laserTarget = -1;           // index into the dominant planet's harvest list (-1 = none)
    int hoverTarget = -1;           // harvestable under the cursor and in range
    int laserProp = -1;             // the same two, for what grows in caves
    int hoverProp = -1;             //   (indices into the dominant planet's caveProps)
    int laserOre = -1;              // ...and for ore the cutter has uncovered
    int hoverOre = -1;              //   (indices into the dominant planet's ores)
    int burnOre = -1;               // the clump the cutter is held on, burning it away
    Vec2 laserEnd;
    float laserDepth = 0.0f;        // near-field depth of whatever the beam ends on
    bool laserHit = false;          // beam ends on something (item or ground), not open sky
    // What the player is carrying, by item id: 0 carbon, 1 ferrite, then one
    // crystal per world archetype (2 + Planet::archetype) — each kind of world
    // grows its own. The names, icons and the panel live in shell.html.
    static constexpr int ITEM_COUNT = 12;
    static constexpr int ITEM_CAVE = 9;         // first of the cave items: calcite, glowcap, gold
    int inventory[ITEM_COUNT] = {0};
    bool inventoryDirty = true;
    // Trading with the locals (Outposts, Trade): what you have been paid, the
    // upgrades bought with it (levels, by Trade::Upgrade), the post whose
    // trade panel is open and the one whose trader is in reach right now
    // (both index the dominant world's outposts; -1 = none).
    int credits = 0;
    bool creditsDirty = true;
    int upgrades[Trade::UPGRADES] = {0};
    int tradePost = -1;
    int nearPost = -1;
    std::string promptShown;        // the "E - ..." hint on the page, as last sent
    int traderHudKey = -2;          // change detector for the HUD's nearest-trader readout
    unsigned int fxSeed = 2463534242u;

    float camAlt = 0.0f;            // eased altitude above the local ground (camera + foreground)
    float camGround = 0.0f;         // radius of that ground (averaged terrain under the player)
    PerlinNoise fgNoise{90210};     // silhouette of the near framing planes

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
    GLuint terrainTex = 0;          // R32F, one row per planet: terrain radius per angle
    int terrainN = 2048;            // its width (entries around the planet)
    GLuint fieldTex = 0;            // R16F mirror of the TerrainField corner grid
    GLuint carveTex = 0;            // R16F mirror of the active planet's CarveMap window
    GLuint waterProgram = 0;        // water: behind the crust, and in front of the walker
    GLuint wetTex = 0;              // R8 mirror of that window's flooded samples

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

static_assert(App::ITEM_COUNT == Trade::ITEMS, "Trade prices cover every inventory item");

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

// The point the camera and all the surface dressing key off: the astronaut
// when on foot, the ship otherwise.
static Vec2 playerPos() { return app.spacewalking ? app.astronaut.pos : app.lander.pos; }

static float smoothstepf(float e0, float e1, float x) {
    float t = (x - e0) / (e1 - e0);
    t = t < 0 ? 0 : (t > 1 ? 1 : t);
    return t * t * (3.0f - 2.0f * t);
}

// Post a one-line event banner to the DOM HUD (discoveries, first landings).
static void showEvent(const std::string& title, const std::string& sub) {
    EM_ASM({
        if (window.showEvent) window.showEvent(UTF8ToString($0), UTF8ToString($1));
    }, title.c_str(), sub.c_str());
}

static constexpr int KEY_R = 82;
static constexpr int KEY_M = 77;
static constexpr float LANDING_MAX_SLOPE = 40.0f; // degrees off the local tangent
static constexpr int MAX_CARGO = 3;
static constexpr int NUM_COWS = 5;

// What the ship holds once the upgrades bought from the traders are counted:
// fuel (the debug knob is the base tank) and cows.
static float maxFuel() { return gDebug.maxFuel * Trade::fuelScale(app.upgrades[Trade::FuelTank]); }
static int maxCargo() { return MAX_CARGO + Trade::cargoBonus(app.upgrades[Trade::CargoBay]); }
// The upgrades that live in other objects' fields.
static void applyUpgrades() {
    app.astronaut.jetpackMax = Astronaut::JETPACK_MAX_FUEL * Trade::jetpackScale(app.upgrades[Trade::Jetpack]);
}
static void closeTrade();   // defined with the trading code

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

static void updateActiveField();   // defined below (lazy destructible fields)

static void resetGame() {
    app.lander.reset();
    app.lander.spawnOnPlanet(homePlanet());
    // Restarting after a crash on another world: bring the home field back
    // before anything (the cows) reads the home surface, so they stand on the
    // dug/gridded ground rather than the analytic silhouette.
    updateActiveField();
    app.state = PLAYING;
    app.cargo = 0;
    app.score = 0;
    app.dominantIdx = app.stuckIdx = app.homeIdx;
    app.mapMode = false;
    app.camAlt = 2000.0f;
    app.particles.clear();
    app.laserOn = false;
    app.laserTarget = app.hoverTarget = -1;
    app.laserProp = app.hoverProp = -1;
    app.laserOre = app.hoverOre = -1;
    app.burnOre = -1;
    for (int i = 0; i < App::ITEM_COUNT; i++) app.inventory[i] = 0;
    app.inventoryDirty = true;
    // A new run starts with empty pockets and a stock ship.
    closeTrade();
    app.credits = 0;
    app.creditsDirty = true;
    for (int i = 0; i < Trade::UPGRADES; i++) app.upgrades[i] = 0;
    applyUpgrades();
    for (Planet& p : app.planets) {
        for (Planet::Outpost& o : p.outposts) {
            o.stock[0] = o.stock[1] = -1;       // the stalls are restocked
            o.met = false;
        }
    }
    spawnCows();
    app.spacewalking      = false;
    app.astronaut.active  = false;

    app.view.focusX = app.lander.pos.x;
    app.view.focusY = app.lander.pos.y;
    // Start already framed for the drop-in altitude (2000 above the ground),
    // not at the ground zoom: otherwise the first second is a lurch outward.
    app.view.scale = std::fmin(gDebug.cameraGroundZoom, app.height * gDebug.cameraFraming / 2000.0f);

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

static void splashDown(const Planet& p, Vec2 at, Vec2 vel, float size);   // defined with the particles

// Track altitude every frame; while PLAYING, judge ground contact as a safe
// landing (slow, upright, flat) or a crash.
static void updateContact() {
    Lander& l = app.lander;
    Planet& p = dominantPlanet();
    float dx = l.pos.x - p.center.x;
    float dy = l.pos.y - p.center.y;
    float d = sqrtf(dx * dx + dy * dy);
    float angle = atan2d(dy, dx);
    // What is under the ship: the ground, or water standing over it — it
    // comes down on either, and floats on the second.
    bool water = p.overWater(angle);
    float alt = d - p.visibleRadius(angle);
    l.altitude = alt > 0 ? alt : 0;

    if (!l.active) return;
    l.updateClearing(p, alt);
    if (alt <= l.radius && !l.clearingGround()) {
        // Speed is judged relative to the body — landing on an orbiting moon
        // means matching its velocity, not the world frame's.
        bool speedSafe = (l.vel - p.orbitVel).length() < Lander::LANDING_MAX_SPEED;
        bool upright = l.isUpright(p);
        bool flat = water || terrainFlat(p, angle);   // water is always level
        app.stuckIdx = app.dominantIdx;
        if (water) splashDown(p, l.pos, l.vel - p.orbitVel, l.radius);
        if (speedSafe && upright && flat) {
            l.land(p);
            app.state = LANDED;
            p.discovered = true;
            if (!p.visited) {
                p.visited = true;
                if (app.dominantIdx != app.homeIdx) {
                    app.score += 100;
                    showEvent("First landing: " + p.name, p.kind + "  +100");
                }
            }
        } else {
            l.crash();
            l.landingOffset = l.pos - p.center;   // the wreck stays on the body
            app.state = CRASHED;
        }
    }
}

// Fullscreen quad covering clip space, shared by all the shader passes.
static void initShaderPasses() {
    app.atmoProgram = Shader::createProgram(kFsVert, kAtmoFrag);
    app.cloudProgram = Shader::createProgram(kFsVert, kCloudFrag);
    app.bodyProgram = Shader::createProgram(kFsVert, kBodyFrag);
    app.waterProgram = Shader::createProgram(kFsVert, kWaterFrag);

    float quad[] = { -1, -1,  1, -1,  -1, 1,  -1, 1,  1, -1,  1, 1 };
    glGenVertexArrays(1, &app.fsVao);
    glGenBuffers(1, &app.fsVbo);
    glBindVertexArray(app.fsVao);
    glBindBuffer(GL_ARRAY_BUFFER, app.fsVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    // Float texture holding the terrain radius per angle (one silhouette row
    // per planet), so the sky/cloud/body shaders know where each ground meets
    // its sky. As wide as the planets' own outline tables (Planet::LUT_N), so
    // the GPU and the CPU read the very same outline — or as wide as this GPU
    // allows, if that is less.
    GLint maxTex = 2048;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTex);
    app.terrainN = Planet::LUT_N;
    while (app.terrainN > maxTex && app.terrainN > 2048) app.terrainN /= 2;
    glGenTextures(1, &app.terrainTex);
    glBindTexture(GL_TEXTURE_2D, app.terrainTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, app.terrainN, SHADER_MAX_PLANETS, 0, GL_RED, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Density-field texture for the body pass; (re)allocated to the grid size
    // in uploadFieldTexture once the planet exists.
    glGenTextures(1, &app.fieldTex);

    // Carve-map texture (what has been dug, in a window round the player):
    // fixed size, filled by uploadCarveTexture / uploadCarveRegion.
    glGenTextures(1, &app.carveTex);
    glBindTexture(GL_TEXTURE_2D, app.carveTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R16F, CarveMap::N, CarveMap::N, 0, GL_RED, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // ...and which of its dug samples the water has got into (CarveMap::wet).
    glGenTextures(1, &app.wetTex);
    glBindTexture(GL_TEXTURE_2D, app.wetTex);
    {
        std::vector<unsigned char> dry((size_t)CarveMap::N * CarveMap::N, 0);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, CarveMap::N, CarveMap::N, 0, GL_RED, GL_UNSIGNED_BYTE, dry.data());
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
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

// Mirror the active planet's carve window into its texture: all of it (after
// the window was rebuilt around the player)...
static void uploadCarveTexture() {
    if (app.activeFieldIdx < 0) return;
    CarveMap& c = app.planets[app.activeFieldIdx].carve;
    if (!c.ready()) return;
    glBindTexture(GL_TEXTURE_2D, app.carveTex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, CarveMap::N, CarveMap::N, GL_RED, GL_FLOAT, c.data().data());
    // The flooded samples go with it.
    c.takeWetDirty();
    glBindTexture(GL_TEXTURE_2D, app.wetTex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, CarveMap::N, CarveMap::N, GL_RED, GL_UNSIGNED_BYTE, c.wet().data());
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
}

// The part of the flooded-samples mirror that changed since it was last sent
// (a bite let the water into more dug space).
static void uploadWetChanges() {
    if (app.activeFieldIdx < 0) return;
    CarveMap& c = app.planets[app.activeFieldIdx].carve;
    if (!c.ready()) return;
    CarveMap::Rect rc = c.takeWetDirty();
    if (rc.empty()) return;
    app.floodedNow += (rc.i1 - rc.i0 + 1) * (rc.j1 - rc.j0 + 1);
    glBindTexture(GL_TEXTURE_2D, app.wetTex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, CarveMap::N);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, rc.i0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, rc.j0);
    glTexSubImage2D(GL_TEXTURE_2D, 0, rc.i0, rc.j0, rc.i1 - rc.i0 + 1, rc.j1 - rc.j0 + 1,
                    GL_RED, GL_UNSIGNED_BYTE, c.wet().data());
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
}

// ...or just the samples one bite touched (UNPACK_ROW_LENGTH copies the
// sub-rect straight out of the full CPU grid). This is the whole GPU cost of
// a dig.
static void uploadCarveRegion(CarveMap::Rect rc) {
    if (rc.empty() || app.activeFieldIdx < 0) return;
    const CarveMap& c = app.planets[app.activeFieldIdx].carve;
    if (!c.ready()) return;
    glBindTexture(GL_TEXTURE_2D, app.carveTex);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, CarveMap::N);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, rc.i0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, rc.j0);
    glTexSubImage2D(GL_TEXTURE_2D, 0, rc.i0, rc.j0, rc.i1 - rc.i0 + 1, rc.j1 - rc.j0 + 1,
                    GL_RED, GL_FLOAT, c.data().data());
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
    uploadWetChanges();
}

// Upload one planet's outline table to that planet's row of the terrain
// texture. Call after its terrain changes.
static void uploadTerrainLUT(int planetIdx) {
    const int N = app.terrainN;
    static std::vector<float> lut;
    lut.resize(N);
    const Planet& p = app.planets[planetIdx];
    for (int k = 0; k < N; k++) {
        lut[k] = p.surfaceRadius(k * (360.0f / N));
    }
    glBindTexture(GL_TEXTURE_2D, app.terrainTex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, planetIdx, N, 1, GL_RED, GL_FLOAT, lut.data());
}

// ---- Day/night light, CPU side (mirror of LIGHT_GLSL) ---------------------
// The per-planet numbers the light model needs, derived from the planet's air.
struct LightParams {
    float extK[3];    // extinction x sunset strength
    float twi[3];     // twilight ambient color
    float night[3];   // night ambient color
    float width;      // twilight half-width in sunDot units
};

static LightParams lightParams(const Planet& p) {
    LightParams L{};
    if (p.isSun || !p.atmosphere) {
        // No air: white light, no afterglow, a narrow hard terminator.
        float n = (0.14f + gDebug.nightDarkness) * 0.40f;
        L.night[0] = L.night[1] = L.night[2] = n;
        L.width = 0.06f;
        return L;
    }
    const float ozone[3] = {0.00065f, 0.00188f, 0.00008f};
    float mx = std::fmax(p.look.skyBeta[0], std::fmax(p.look.skyBeta[1], p.look.skyBeta[2]));
    float level = 0.14f + gDebug.nightDarkness;       // 0.30 at the default knob
    for (int k = 0; k < 3; k++) {
        float ext = p.look.skyBeta[k] + p.look.mieBeta[k] + ozone[k];
        L.extK[k] = ext * gDebug.atmExtinction;
        float tint = mx > 0 ? p.look.skyBeta[k] / mx : 1.0f;
        // Twilight = what is left of the sunlight at the horizon (deep red on
        // a blue-sky world) plus the sky's own scattered color: rose.
        float sunLow = expf(-L.extK[k] * 11.7f);
        L.twi[k] = (sunLow * 1.25f + tint * 0.42f) * 0.85f;
        L.night[k] = level * (0.65f + 0.60f * tint);
    }
    L.width = gDebug.nightSoftness;
    return L;
}

static float lightDirect(const LightParams& L, float s) {
    return smoothstepf(-0.33f * L.width, L.width, s);
}

// Color of the light on the ground where the sun stands at sine-elevation s.
static void groundLight(const LightParams& L, float s, float out[3]) {
    float w = L.width;
    float airmass = 1.0f / std::fmax(s * 0.9f + 0.1f, 0.05f);
    float excess = std::fmax(airmass - 2.0f, 0.0f);
    float direct = lightDirect(L, s);
    float twi = smoothstepf(-1.6f * w, -0.2f * w, s) * (1.0f - smoothstepf(-0.17f * w, 0.8f * w, s));
    float night = 1.0f - smoothstepf(-1.6f * w, 0.0f, s);
    for (int k = 0; k < 3; k++)
        out[k] = expf(-L.extK[k] * excess) * direct + L.twi[k] * twi + L.night[k] * night;
}

// Light where a planet-relative direction points (angle in degrees).
static void groundLightAt(const Planet& p, const LightParams& L, Vec2 toSun, float angleDeg, float out[3]) {
    (void)p;
    groundLight(L, cosd(angleDeg) * toSun.x + sind(angleDeg) * toSun.y, out);
}

// Upload the light-model arrays (LIGHT_GLSL) to the bound program.
static void setLightUniforms(GLuint pr) {
    const int N = SHADER_MAX_PLANETS;
    float extK[N * 3] = {0}, twi[N * 3] = {0}, night[N * 3] = {0}, width[N];
    for (int i = 0; i < N; i++) width[i] = 0.3f;
    int count = (int)app.planets.size();
    if (count > N) count = N;
    for (int i = 0; i < count; i++) {
        LightParams L = lightParams(app.planets[i]);
        for (int k = 0; k < 3; k++) {
            extK[i * 3 + k] = L.extK[k];
            twi[i * 3 + k] = L.twi[k];
            night[i * 3 + k] = L.night[k];
        }
        width[i] = L.width;
    }
    glUniform3fv(glGetUniformLocation(pr, "u_extK"), N, extK);
    glUniform3fv(glGetUniformLocation(pr, "u_twiCol"), N, twi);
    glUniform3fv(glGetUniformLocation(pr, "u_nightAmb"), N, night);
    glUniform1fv(glGetUniformLocation(pr, "u_twiWidth"), N, width);
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
// Main: the bodies. Veil: the front ground laid back over holes. CaveMask:
// no color, only the cave's open space marked in the stencil (drawCaveMask).
enum class BodyPass { Main, Veil, CaveMask };
static bool drawBodyPass(BodyPass pass = BodyPass::Main);   // defined after sunWorldPos (uses it)

// World position of the sun — the actual star body. It kinematically orbits the
// home planet (see init) at the old day-cycle rate, so the drifting terminator,
// sunsets, and cloud shading behave exactly as they did with the fake sun lamp.
// This single point is the light source for every pass AND where the visible
// sun is drawn, so they all agree.
static Vec2 sunWorldPos() {
    return app.planets[app.sunIdx].center;
}

// How far the player has climbed out of the dominant body's sky: 0 from the
// ground up through the lower air, ramping to 1 across the upper atmosphere so
// the changeover completes right at the top. Airless bodies use the height an
// atmosphere WOULD have, so their surface dressing fades on the same schedule.
static float climbRamp() {
    const Planet& p = dominantPlanet();
    Vec2 pp = playerPos();
    float distC = dist(pp.x, pp.y, p.center.x, p.center.y);
    float thickness = p.atmosphere ? p.atmosphereThickness()
                                   : p.baseRadius * gDebug.atmosphereScale;
    float altFrac = (distC - p.baseRadius) / (thickness > 1.0f ? thickness : 1.0f);
    return altFrac <= 0.55f ? 0.0f
         : altFrac >= 1.0f  ? 1.0f
         : (altFrac - 0.55f) / (1.0f - 0.55f);
}

// How strongly the sky + clouds fill the planet disk: 0 near the surface (so
// the ground stays clear in front of you) ramping to gDebug.spaceHaze once
// you're clearly out in space. This makes the atmosphere read as a full sphere
// with the planet showing through.
static float spaceOverlay() {
    return climbRamp() * gDebug.spaceHaze;
}

// Strength of everything that only makes sense seen from the ground — ridge
// layers, cast shadows, light shafts, sky-dome bodies, motes. Fades out on the
// climb to space AND when the camera is pulled far back (wheel zoom / map), so
// none of it is ever left hanging around a planet that has become a small disc.
static float surfaceDressing() {
    float zoomK = app.view.scale / (gDebug.cameraGroundZoom > 1e-4f ? gDebug.cameraGroundZoom : 1e-4f);
    return (1.0f - climbRamp()) * smoothstepf(0.10f, 0.30f, zoomK);
}

// ============================================================================
// Mining laser + harvesting (on foot). Hold the mouse button to fire at the
// cursor; plants and minerals on the ground strip (Planet::harvest, placed in
// depth by nearfield.cpp) come apart under the beam and fly to the player.
// ============================================================================
static constexpr float LASER_RANGE = 300.0f;        // world units from the player
// Terrain cutter (the tool's second mode).
static constexpr float CUT_REACH = 120.0f;          // how far the cutter reaches
static constexpr float CUT_RADIUS = 20.0f;          // a bite is a circle this big (a tunnel 40 high: the figure needs 36.5)
static constexpr float CUT_BITE = 3.0f;             // how deep each bite goes into the face
static constexpr float CUT_INTERVAL = 0.06f;        // seconds between bites while held (~50 units/s)

static float fxRand() {
    app.fxSeed ^= app.fxSeed << 13;
    app.fxSeed ^= app.fxSeed >> 17;
    app.fxSeed ^= app.fxSeed << 5;
    return (app.fxSeed & 0xffffffu) * (1.0f / 16777216.0f);
}

// World point under a canvas pixel (the inverse of View::matrix; the same
// reconstruction the fullscreen shaders do per fragment).
static Vec2 screenToWorld(float mx, float my) {
    float cx = mx - app.width * 0.5f, cy = my - app.height * 0.5f;
    float c = cosd(-app.view.rotation), s = sind(-app.view.rotation);
    return Vec2((c * cx - s * cy) / app.view.scale + app.view.focusX,
                (s * cx + c * cy) / app.view.scale + app.view.focusY);
}

// The slice of the near field that is on screen right now.
static NearField::Frame nearFrame(float time) {
    const Planet& p = dominantPlanet();
    NearField::Frame f;
    f.viewAngle = atan2d(app.view.focusY - p.center.y, app.view.focusX - p.center.x);
    float halfDiag = 0.5f * sqrtf((float)app.width * app.width + (float)app.height * app.height);
    f.halfSpanDeg = halfDiag / app.view.scale / (p.baseRadius > 1.0f ? p.baseRadius : 1.0f) * RAD2DEG * 1.15f;
    f.time = time;
    f.toSun = (app.planets[app.sunIdx].center - p.center).normalize();
    f.strength = surfaceDressing();
    // One light for the whole strip on screen (it spans a degree or two).
    groundLight(lightParams(p), cosd(f.viewAngle) * f.toSun.x + sind(f.viewAngle) * f.toSun.y, f.light);
    return f;
}

// The same for what grows in the caves (CaveField).
static CaveField::Frame caveFrame(float time) {
    const Planet& p = dominantPlanet();
    CaveField::Frame f;
    f.focus = Vec2(app.view.focusX, app.view.focusY);
    f.reach = 0.5f * sqrtf((float)app.width * app.width + (float)app.height * app.height) / app.view.scale * 1.1f;
    f.time = time;
    float viewAngle = atan2d(app.view.focusY - p.center.y, app.view.focusX - p.center.x);
    Vec2 toSun = (app.planets[app.sunIdx].center - p.center).normalize();
    groundLight(lightParams(p), cosd(viewAngle) * toSun.x + sind(viewAngle) * toSun.y, f.light);
    f.inside = app.inHole;
    f.holeFloor = gDebug.holeFloor;
    f.holeDepth = gDebug.holeDepth;
    f.strength = app.dominantIdx == app.activeFieldIdx ? surfaceDressing() : 0.0f;
    return f;
}

static void spawnParticle(Vec2 world, Vec2 vel, float life, float size, const float* col, int mode) {
    if (app.particles.size() > 400) return;
    App::Particle q;
    q.local = world - dominantPlanet().center;
    q.vel = vel;
    q.life = q.maxLife = life;
    q.size = size;
    q.r = col[0]; q.g = col[1]; q.b = col[2];
    q.mode = mode;
    app.particles.push_back(q);
}

// ---- Water effects ----------------------------------------------------------
// Things are drawn on the walking line, NearField::WALK_DROP below where they
// physically are, and so is the water round them (see kWaterFrag): on screen
// its surface meets a wader, a swimmer or a floating hull at this radius.
static float drawnWaterline(const Planet& p) { return p.seaRadius() - NearField::WALK_DROP; }

// The color of spray and ripples: the water's own, most of the way to white,
// in the light of the hour at that spot.
static void sprayColor(const Planet& p, Vec2 world, float out[3]) {
    Vec2 up = (world - p.center).normalize();
    Vec2 toSun = (app.planets[app.sunIdx].center - p.center).normalize();
    float light[3];
    groundLight(lightParams(p), up.dot(toSun), light);
    for (int k = 0; k < 3; k++) {
        float v = (p.look.water[k] * 0.35f + 0.65f) * light[k];
        out[k] = v > 1.0f ? 1.0f : v;
    }
}

// A bubble let go at a (drawn) point under water: it wobbles up and is gone at
// the surface.
static void spawnBubble(const Planet& p, Vec2 drawn, float size) {
    float col[3];
    sprayColor(p, drawn, col);
    Vec2 up = (drawn - p.center).normalize();
    Vec2 side(-up.y, up.x);
    spawnParticle(drawn, up * (0.25f + fxRand() * 0.35f) + side * ((fxRand() - 0.5f) * 0.5f),
                  1.6f + fxRand() * 1.6f, size, col, 5);
}

// A ring spreading on the water over a (drawn or physical) point.
static void spawnRipple(const Planet& p, Vec2 over, float size) {
    float col[3];
    sprayColor(p, over, col);
    Vec2 up = (over - p.center).normalize();
    spawnParticle(p.center + up * drawnWaterline(p), Vec2(0, 0), 0.9f + fxRand() * 0.4f, size, col, 6);
}

// Something has hit the water at `at`, moving at `vel` (relative to the
// world): spray thrown up and out from where it went in, and rings.
static void splashDown(const Planet& p, Vec2 at, Vec2 vel, float size) {
    float col[3];
    sprayColor(p, at, col);
    Vec2 up = (at - p.center).normalize();
    Vec2 side(-up.y, up.x);
    Vec2 where = p.center + up * (drawnWaterline(p) + 1.0f);
    float speed = vel.length();
    int count = (int)(6.0f + speed * 5.0f + size * 0.4f);
    if (count > 46) count = 46;
    float kick = 1.4f + (speed < 8.0f ? speed : 8.0f) * 0.45f;
    for (int i = 0; i < count; i++) {
        // Mostly fine drops thrown wide; a few heavier ones that go less far.
        bool heavy = fxRand() < 0.25f;
        float lift = 0.5f + fxRand() * 0.8f, fan = (fxRand() - 0.5f) * 2.2f;
        float sp = kick * (0.5f + fxRand() * 0.9f) * (heavy ? 0.7f : 1.0f);
        spawnParticle(where + side * ((fxRand() - 0.5f) * size * 2.0f),
                      (up * lift + side * fan) * sp + side * (vel.dot(side) * 0.25f),
                      0.55f + fxRand() * 0.6f, heavy ? 1.5f + fxRand() * 0.9f : 0.7f + fxRand() * 0.7f, col, 4);
    }
    spawnRipple(p, at, size * 0.45f);
    spawnRipple(p, at + side * (size * 0.3f), size * 0.2f);
}

// Add to the inventory and tell the page (pickup feed + panel refresh).
static void addItem(int id, int amount) {
    if (id < 0 || id >= App::ITEM_COUNT || amount <= 0) return;
    app.inventory[id] += amount;
    app.inventoryDirty = true;
    EM_ASM({ if (window.notifyPickup) window.notifyPickup($0, $1); }, id, amount);
}

// The inventory item a resource (NearField::Yield::resource) goes to: 0
// carbon, 1 ferrite, 2 the crystal of this kind of world, and from 3 the
// caves' own (calcite, glowcap, gold).
static int resourceItem(const Planet& p, int resource) {
    if (resource == 2) return 2 + p.archetype;
    if (resource >= 3) return App::ITEM_CAVE + (resource - 3);
    return resource;
}

// ============================================================================
// Trading with the locals. The posts and their people are Outposts' (where
// they are, how they look); the prices and the upgrade catalog are Trade's;
// this is the till: the open panel, the money, and what the page's Sell / Buy
// buttons call. The panel itself is shell.html's.
// ============================================================================
static void addCredits(int amount) {
    if (amount == 0) return;
    app.credits += amount;
    app.creditsDirty = true;
    EM_ASM({ if (window.notifyCredits) window.notifyCredits($0); }, amount);
}

// On foot, would E trade rather than board the ship? Whichever is nearer,
// when both are in reach.
static bool wouldTrade() {
    if (!app.spacewalking || app.nearPost < 0) return false;
    const Planet& p = dominantPlanet();
    if (app.nearPost >= (int)p.outposts.size()) return false;
    Vec2 g = Outposts::ground(p, p.outposts[app.nearPost]);
    Vec2 a = app.astronaut.pos;
    float postDist = dist(a.x, a.y, g.x, g.y);
    float shipDist = dist(a.x, a.y, app.lander.pos.x, app.lander.pos.y);
    return shipDist >= app.lander.radius * 4.0f || postDist <= shipDist;
}

// Send the open panel its contents: who you are dealing with, what they pay
// for each thing you carry, what they sell and what is left of it.
static void pushTrade() {
    if (app.tradePost < 0) return;
    Planet& p = dominantPlanet();
    if (app.tradePost >= (int)p.outposts.size()) { app.tradePost = -1; return; }
    Planet::Outpost& o = p.outposts[app.tradePost];
    auto num = [](int v) { return std::to_string(v); };
    std::string j = "{\"keeper\":\"" + o.keeper + "\",\"world\":\"" + p.name + "\",\"say\":\"" +
                    Outposts::phrase(o) + "\",\"line\":" + num((int)(o.seed % 9973u)) +
                    ",\"want\":" + num(Trade::wanted(p, o)) + ",\"credits\":" + num(app.credits) + ",\"sell\":[";
    bool first = true;
    auto sellRow = [&](int id, int n) {
        if (n <= 0) return;
        j += std::string(first ? "" : ",") + "{\"id\":" + num(id) + ",\"n\":" + num(n) + ",\"p\":" +
             num(Trade::sellPrice(p, o, id)) + ",\"t\":" + num(Trade::trend(p, o, id)) + "}";
        first = false;
    };
    for (int i = 0; i < App::ITEM_COUNT; i++) sellRow(i, app.inventory[i]);
    sellRow(Trade::COW, app.cargo);
    j += "],\"up\":[";
    for (int u = 0; u < Trade::UPGRADES; u++) {
        j += std::string(u ? "," : "") + "{\"id\":" + num(u) + ",\"lvl\":" + num(app.upgrades[u]) + ",\"max\":" +
             num(Trade::maxLevel(u)) + ",\"p\":" + num(Trade::upgradePrice(u, app.upgrades[u])) +
             ",\"now\":" + num(Trade::effect(u, app.upgrades[u])) + ",\"next\":" +
             num(Trade::effect(u, app.upgrades[u] + 1)) + "}";
    }
    j += "],\"goods\":[";
    for (int s = 0; s < Trade::GOODS; s++) {
        j += std::string(s ? "," : "") + "{\"slot\":" + num(s) + ",\"id\":" + num(Trade::goodsItem(p, o, s)) +
             ",\"stock\":" + num(Trade::goodsStock(o, s)) + ",\"p\":" + num(Trade::goodsPrice(p, o, s)) + "}";
    }
    j += "]}";
    EM_ASM({ if (window.showTrade) window.showTrade(UTF8ToString($0)); }, j.c_str());
}

static void openTrade(int post) {
    Planet& p = dominantPlanet();
    if (post < 0 || post >= (int)p.outposts.size()) return;
    app.tradePost = post;
    p.outposts[post].met = true;
    pushTrade();
}

static void closeTrade() {
    if (app.tradePost < 0) return;
    app.tradePost = -1;
    EM_ASM({ if (window.hideTrade) window.hideTrade(); });
}

// A deal done: a few motes in the credits' teal pass between the trader and
// the player.
static void tradeSparkle(const Planet& p, const Planet::Outpost& o, int count) {
    static const float teal[3] = {0.30f, 0.86f, 0.80f};
    Vec2 from = Outposts::traderHead(p, o);
    for (int i = 0; i < count; i++) {
        float ang = fxRand() * 360.0f, sp = 1.0f + fxRand() * 2.4f;
        spawnParticle(from + Vec2(cosd(ang), sind(ang)) * (4.0f * fxRand()), Vec2(cosd(ang) * sp, sind(ang) * sp),
                      0.8f + fxRand() * 0.5f, 1.8f + fxRand() * 1.4f, teal, 1);
    }
}

// The panel's buttons. Each returns what it did (credits earned / 1 or 0 /
// how many bought) and refreshes the panel.
//
// Sell to the post whose panel is open: `count` of `item` (an inventory id,
// or Trade::COW for a cow out of the ship's hold); count <= 0 = all of it;
// item < 0 = everything being carried (the cows stay).
extern "C" EMSCRIPTEN_KEEPALIVE int tradeSell(int item, int count) {
    if (app.tradePost < 0) return 0;
    Planet& p = dominantPlanet();
    if (app.tradePost >= (int)p.outposts.size()) return 0;
    Planet::Outpost& o = p.outposts[app.tradePost];
    int earned = 0;
    auto sell = [&](int id, int n) {
        int& have = id == Trade::COW ? app.cargo : app.inventory[id];
        if (n <= 0 || n > have) n = have;
        if (n <= 0) return;
        have -= n;
        earned += n * Trade::sellPrice(p, o, id);
    };
    if (item < 0) {
        for (int i = 0; i < App::ITEM_COUNT; i++) sell(i, 0);
    } else if (item == Trade::COW || item < App::ITEM_COUNT) {
        sell(item, count);
    }
    if (earned <= 0) return 0;
    app.inventoryDirty = true;
    addCredits(earned);
    tradeSparkle(p, o, earned > 200 ? 10 : 6);
    pushTrade();
    return earned;
}

// Buy the next level of an upgrade (Trade::Upgrade).
extern "C" EMSCRIPTEN_KEEPALIVE int tradeBuyUpgrade(int id) {
    if (app.tradePost < 0 || id < 0 || id >= Trade::UPGRADES) return 0;
    int level = app.upgrades[id];
    if (level >= Trade::maxLevel(id)) return 0;
    int price = Trade::upgradePrice(id, level);
    if (app.credits < price) return 0;
    addCredits(-price);
    app.upgrades[id] = level + 1;
    applyUpgrades();
    EM_ASM({ if (window.notifyUpgrade) window.notifyUpgrade($0, $1, $2); }, id, level + 1,
           Trade::effect(id, level + 1));
    pushTrade();
    return 1;
}

// Buy `count` from a goods slot (as many as the stock and the money allow).
extern "C" EMSCRIPTEN_KEEPALIVE int tradeBuyGoods(int slot, int count) {
    if (app.tradePost < 0 || slot < 0 || slot >= Trade::GOODS) return 0;
    Planet& p = dominantPlanet();
    if (app.tradePost >= (int)p.outposts.size()) return 0;
    Planet::Outpost& o = p.outposts[app.tradePost];
    int stock = Trade::goodsStock(o, slot), price = Trade::goodsPrice(p, o, slot);
    if (count > stock) count = stock;
    if (price > 0 && count > app.credits / price) count = app.credits / price;
    if (count <= 0) return 0;
    o.stock[slot] -= count;
    addCredits(-count * price);
    addItem(Trade::goodsItem(p, o, slot), count);
    pushTrade();
    return count;
}

extern "C" EMSCRIPTEN_KEEPALIVE void tradeClose() { closeTrade(); }

// Ore just uncovered (p.ores from `before` on): a glint of sparks in its
// color, and a line in the pickup feed.
static void oreFound(const Planet& p, size_t before) {
    for (size_t i = before; i < p.ores.size(); i++) {
        const Planet::Ore& o = p.ores[i];
        float col[3];
        Deposits::color(p, o.kind, col);
        Vec2 c = Deposits::center(p, o);
        for (int k = 0; k < 8; k++) {
            float ang = fxRand() * 360.0f, sp = 0.8f + fxRand() * 1.8f;
            spawnParticle(c + Vec2(cosd(ang), sind(ang)) * (o.size * 0.5f), Vec2(cosd(ang) * sp, sind(ang) * sp),
                          0.45f + fxRand() * 0.35f, 1.6f + fxRand() * 1.2f, col, 0);
        }
        int item = resourceItem(p, Deposits::yield(o).resource);
        EM_ASM({ if (window.notifyFind) window.notifyFind($0); }, item);
    }
}

// The fine dig layer's window was just rebuilt around the player: mirror it
// to the GPU, and show the ore already lying in the walls it now covers.
static void carveRefocused(Planet& p) {
    uploadCarveTexture();
    Deposits::scanWindow(p);
}

// The terrain cutter held on a clump of ore: it heats — the beam ends on it,
// throwing sparks, then embers and smoke as it reddens — and after
// Deposits::burnSeconds it burns away, pocket and all. Nothing is collected.
static void burnOre(Planet& p, int idx, float dt, Vec2 tool) {
    Planet::Ore& o = p.ores[idx];
    app.burnOre = idx;
    Vec2 mid = Deposits::center(p, o);
    Vec2 toTool = (tool - mid).normalize();
    Vec2 face = mid + toTool * (o.size * 0.95f);
    app.laserEnd = face;
    app.laserHit = true;
    o.heat += dt / Deposits::burnSeconds(o.kind);
    float heat = o.heat < 1.0f ? o.heat : 1.0f;

    float col[3];
    Deposits::color(p, o.kind, col);
    static const float ember[3] = {1.0f, 0.48f, 0.14f};
    static const float red[3] = {1.0f, 0.22f, 0.08f};
    static const float smoke[3] = {0.16f, 0.15f, 0.15f};
    Vec2 up = (mid - p.center).normalize();
    float back = atan2d(toTool.y, toTool.x);
    float rate = dt * 60.0f;                 // per-frame chances, at 60 Hz
    // Sparks in the ore's own color while it is cold, giving way to embers
    // that fall, redder and more of them the hotter it gets.
    if (fxRand() < (0.35f - 0.25f * heat) * rate) {
        float ang = back + (fxRand() - 0.5f) * 130.0f, sp = 2.0f + fxRand() * 3.0f;
        spawnParticle(face, Vec2(cosd(ang) * sp, sind(ang) * sp), 0.22f + fxRand() * 0.25f,
                      1.5f + fxRand() * 1.2f, col, 0);
    }
    if (fxRand() < (0.06f + 0.34f * heat) * rate) {
        float ang = fxRand() * 360.0f, sp = 0.8f + fxRand() * (1.6f + 1.8f * heat);
        Vec2 off(cosd(ang) * o.size * 0.7f, sind(ang) * o.size * 0.7f);
        spawnParticle(mid + off, Vec2(cosd(ang) * sp, sind(ang) * sp) + up * 0.6f, 0.5f + fxRand() * 0.6f,
                      1.3f + fxRand() * 1.4f, fxRand() < heat ? red : ember, 2);
    }
    if (heat > 0.25f && fxRand() < 0.07f * heat * rate) {
        Vec2 drift = up * (0.5f + fxRand() * 0.5f) + Vec2((fxRand() - 0.5f) * 0.5f, (fxRand() - 0.5f) * 0.5f);
        spawnParticle(mid + up * (o.size * 0.6f), drift, 0.9f + fxRand() * 0.6f, 3.0f + fxRand() * 2.5f, smoke, 3);
    }
    if (o.heat < 1.0f) return;

    // Gone: a burst of embers, cinders of what it was, smoke, and scorched
    // rock where it sat.
    const float cinder[3] = {col[0] * 0.22f, col[1] * 0.20f, col[2] * 0.20f};
    static const float flash[3] = {1.0f, 0.86f, 0.55f};
    float rad = o.size;
    for (int i = 0; i < 16; i++) {
        float ang = fxRand() * 360.0f, sp = 1.5f + fxRand() * 4.0f;
        Vec2 off(cosd(ang) * o.size * 0.5f * fxRand(), sind(ang) * o.size * 0.5f * fxRand());
        spawnParticle(mid + off, Vec2(cosd(ang) * sp, sind(ang) * sp) + up * 1.0f, 0.6f + fxRand() * 0.7f,
                      1.5f + fxRand() * 1.8f, i % 3 == 0 ? red : ember, 2);
    }
    for (int i = 0; i < 8; i++) {
        float ang = fxRand() * 360.0f, sp = 0.8f + fxRand() * 2.0f;
        Vec2 off(cosd(ang) * rad * 0.6f * fxRand(), sind(ang) * rad * 0.6f * fxRand());
        spawnParticle(mid + off, Vec2(cosd(ang) * sp, sind(ang) * sp), 0.5f + fxRand() * 0.5f,
                      1.8f + fxRand() * 1.8f, cinder, 2);
    }
    for (int i = 0; i < 10; i++) {
        float ang = fxRand() * 360.0f, sp = 4.0f + fxRand() * 5.0f;
        spawnParticle(mid + Vec2(cosd(ang), sind(ang)) * (rad * 0.5f), Vec2(cosd(ang) * sp, sind(ang) * sp),
                      0.16f + fxRand() * 0.18f, 1.6f + fxRand() * 1.4f, flash, 0);
    }
    for (int i = 0; i < 3; i++) {
        float ang = fxRand() * 360.0f;
        Vec2 drift = up * (0.5f + fxRand() * 0.6f) + Vec2((fxRand() - 0.5f) * 0.8f, (fxRand() - 0.5f) * 0.8f);
        spawnParticle(mid + Vec2(cosd(ang), sind(ang)) * (rad * 0.7f), drift, 1.0f + fxRand() * 0.7f,
                      3.0f + fxRand() * 2.5f, smoke, 3);
    }
    App::Burn& b = app.burns[app.burnNext];
    app.burnNext = (app.burnNext + 1) % App::MAX_BURNS;
    b.local = o.local;
    b.heat = 1.0f;
    b.radius = (o.size + Deposits::POCKET) * 1.3f;
    EM_ASM({ if (window.notifyLost) window.notifyLost($0); }, resourceItem(p, Deposits::yield(o).resource));
    // Its pocket is burnt out of the rock, which can show the next clump of
    // the vein. (That can add to p.ores: `o` is not used past here.)
    size_t known = p.ores.size();
    uploadCarveRegion(Deposits::extract(p, idx));
    oreFound(p, known);
    app.burnOre = -1;
}

// Aim, cut, harvest. Runs after the astronaut has moved this frame.
static void updateLaser(float dt, float time) {
    Astronaut& a = app.astronaut;
    Planet& p = dominantPlanet();
    app.laserOn = false;
    app.laserTarget = -1;
    app.hoverTarget = -1;
    app.laserProp = -1;
    app.hoverProp = -1;
    app.laserOre = -1;
    app.hoverOre = -1;
    app.burnOre = -1;
    a.aiming = false;
    float mx = Input::getMouseX(), my = Input::getMouseY();
    // (Not while the trade panel is up: the mouse is busy with that.)
    if (!app.spacewalking || mx < 0.0f || app.mapMode || app.tradePost >= 0) return;

    NearField::Frame f = nearFrame(time);
    Vec2 cursor = screenToWorld(mx, my);
    float slack = 7.0f / app.view.scale;                 // a few pixels of forgiveness
    bool cutter = app.toolMode == 1;
    app.laserCutting = cutter;
    if (!cutter) {
        app.hoverTarget = NearField::pick(p, f, cursor, a.pos, LASER_RANGE, slack);
        // What grows in the caves, and ore the cutter has uncovered.
        // Underground that is what you are aiming at; out on the surface a
        // plant under the cursor comes first.
        Vec2 eye = a.pos + (a.pos - p.center).normalize() * 16.0f;
        CaveField::Frame cf = caveFrame(time);
        app.hoverProp = CaveField::pick(p, cf, cursor, a.pos, eye, LASER_RANGE, slack);
        app.hoverOre = Deposits::pick(p, cf, cursor, a.pos, eye, LASER_RANGE, slack);
        if (app.hoverOre >= 0) app.hoverProp = -1;     // (ore is in the wall, behind anything growing there)
        if ((app.hoverProp >= 0 || app.hoverOre >= 0) && app.hoverTarget >= 0) {
            if (app.inHole < 0.5f) app.hoverProp = app.hoverOre = -1;
            else app.hoverTarget = -1;
        }
    }
    if (!Input::isMouseDown()) { app.cutTimer = 0.0f; return; }

    app.laserOn = true;
    a.aiming = true;
    static const float sparkCol[3] = {1.0f, 0.78f, 0.50f};
    // The tool upgrades the traders sell: the laser mines faster, the cutter
    // bites more often.
    const float mineDt = dt * Trade::laserScale(app.upgrades[Trade::Laser]);
    const float cutInterval = CUT_INTERVAL / Trade::cutterScale(app.upgrades[Trade::Cutter]);
    if (cutter) {
        // ---- Terrain cutter ---------------------------------------------------
        // Things on the walking line are drawn WALK_DROP below where they
        // physically are (see the body shader), so what the cursor points at
        // is that far above it in the physical world: aim there. The beam runs
        // from the physical height of the tool toward it and bites the first
        // rock it meets, a little at a time. Each bite is a circle a bit over
        // one body-height across set almost entirely on the near side of the
        // rock face, so it shaves CUT_BITE off the surface with a rounded
        // profile: hold it and the hole deepens steadily under the beam. A
        // level beam leaves a tunnel whose floor is the floor you are standing
        // on, tall enough to walk in.
        Vec2 up = (a.pos - p.center).normalize();
        Vec2 drop = up * NearField::WALK_DROP;
        Vec2 origin = a.pos + up * (CUT_RADIUS - Astronaut::FOOT_R);
        Vec2 d = (cursor + drop) - origin;
        float len = d.length();
        Vec2 dir = len > 1.0f ? d * (1.0f / len) : up * -1.0f;
        float reach = len + 12.0f < CUT_REACH ? len + 12.0f : CUT_REACH;
        float hitT = -1.0f;
        for (float t = 2.0f; t <= reach; t += 2.0f) {
            Vec2 q = origin + dir * t;
            if (p.rockAt(q) > -0.5f) { hitT = t; break; }
        }
        app.laserDepth = 0.0f;
        if (hitT < 0.0f) {
            app.laserEnd = origin + dir * (len < CUT_REACH ? len : CUT_REACH) - drop;
            app.laserHit = false;
            app.cutTimer = 0.0f;
        } else {
            Vec2 hit = origin + dir * hitT;
            Vec2 hitDrawn = hit - drop;            // where that shows on screen
            app.laserEnd = hitDrawn;
            app.laserHit = true;
            // Not under the ship: it stands on the terrain's outline, and
            // would be left hanging over the hole. Nor under a trading post,
            // for the same reason (and it would be rude).
            bool nearShip = dist(hit.x, hit.y, app.lander.pos.x, app.lander.pos.y) < 90.0f ||
                            Outposts::blocksDig(p, hit, CUT_RADIUS);
            Vec2 c = hit - dir * (CUT_RADIUS - CUT_BITE);
            // Ore stops the cutter: a bite that would cut into a clump is
            // refused, and leaves it showing in the wall for the mining
            // laser. Keep the cutter on it and it heats instead — red, then
            // orange — until it burns away, taking far longer than rock does.
            size_t known = p.ores.size();
            int ore = nearShip ? -1 : Deposits::blocking(p, c, CUT_RADIUS);
            oreFound(p, known);
            if (ore >= 0) {
                app.cutTimer = 0.0f;
                burnOre(p, ore, dt, a.pos);
            }
            app.cutTimer += dt;
            if (ore < 0 && app.cutTimer >= cutInterval && !nearShip) {
                app.cutTimer = 0.0f;
                bool wasDry = p.waterDepth(a.pos + up * 4.0f) <= 0.0f;
                app.floodedNow = 0;
                CarveMap::Rect rc = p.dig(c, CUT_RADIUS);
                if (!rc.empty()) {
                    uploadCarveRegion(rc);
                    // Cut through into water from a dry tunnel, and it comes
                    // in: far more space floods than the one bite opened. A
                    // rush of bubbles from the breach back along the tunnel.
                    if (wasDry && app.floodedNow > 500) {
                        for (int i = 0; i < 26; i++) {
                            float back = fxRand();
                            Vec2 at = hitDrawn + (a.pos - drop - hitDrawn) * back +
                                      Vec2((fxRand() - 0.5f) * 30.0f, (fxRand() - 0.5f) * 30.0f);
                            spawnBubble(p, at, 1.4f + fxRand() * 2.6f);
                        }
                    }
                    // A roof only a few units thick over the cut is not a roof:
                    // open it to the sky, rather than leave a sliver of ground
                    // that looks like air but holds you up.
                    Vec2 cUp = (c - p.center).normalize();
                    float cr = dist(c.x, c.y, p.center.x, p.center.y);
                    float roof = p.surfaceRadius(atan2d(cUp.y, cUp.x)) - (cr + CUT_RADIUS);
                    if (roof > 0.0f && roof < 12.0f) {
                        Vec2 lid = c + cUp * (roof + 6.0f);
                        if (Deposits::blocking(p, lid, CUT_RADIUS) < 0) {
                            uploadCarveRegion(p.dig(lid, CUT_RADIUS));
                            Deposits::revealNear(p, lid, CUT_RADIUS);
                        }
                    }
                    // Ore the bite has brought into view.
                    Deposits::revealNear(p, c, CUT_RADIUS);
                    oreFound(p, known);
                    // Anything growing on the ground that just went goes with it.
                    NearField::uproot(p, f, c, CUT_RADIUS + 4.0f);
                    // The rock around the bite stays hot for a moment.
                    App::Burn& b = app.burns[app.burnNext];
                    app.burnNext = (app.burnNext + 1) % App::MAX_BURNS;
                    b.local = hit - p.center;
                    b.heat = 1.0f;
                    b.radius = CUT_RADIUS * 1.3f;
                    // Rock chips, embers that fall, a little smoke. Near ore
                    // that is still hidden some of the chips glitter in its
                    // color — the closer, the more: you are on to something.
                    static const float ember[3] = {1.0f, 0.55f, 0.18f};
                    static const float smoke[3] = {0.16f, 0.15f, 0.15f};
                    Planet::Ore::Kind nearKind = Planet::Ore::Ferrite;
                    float near = Deposits::glitter(p, c, CUT_RADIUS, &nearKind);
                    float glint[3];
                    Deposits::color(p, nearKind, glint);
                    for (int i = 0; i < 2; i++) {
                        float ang = atan2d(-dir.y, -dir.x) + (fxRand() - 0.5f) * 110.0f;
                        float sp = 1.5f + fxRand() * 3.0f;
                        bool glitters = near > 0.0f && fxRand() < 0.08f + 0.6f * near;
                        spawnParticle(hitDrawn, Vec2(cosd(ang) * sp, sind(ang) * sp),
                                      0.35f + fxRand() * 0.35f, 2.0f + fxRand() * 1.6f,
                                      glitters ? glint : p.look.tones[2], 0);
                    }
                    if (p.waterDepth(hit - dir * 6.0f) > 0.0f) {
                        // Under water nothing burns or smokes: the cut boils,
                        // and a string of bubbles goes up from it.
                        for (int i = 0; i < 2; i++)
                            spawnBubble(p, hitDrawn + Vec2((fxRand() - 0.5f) * 14.0f, (fxRand() - 0.5f) * 14.0f),
                                        1.2f + fxRand() * 1.8f);
                    } else {
                        for (int i = 0; i < 2; i++) {
                            float ang = atan2d(-dir.y, -dir.x) + (fxRand() - 0.5f) * 150.0f;
                            float sp = 1.0f + fxRand() * 2.6f;
                            spawnParticle(hitDrawn, Vec2(cosd(ang) * sp, sind(ang) * sp),
                                          0.55f + fxRand() * 0.55f, 1.4f + fxRand() * 1.3f, ember, 2);
                        }
                        if (app.cutBites % 3 == 0) {
                            Vec2 drift = up * (0.5f + fxRand() * 0.5f) + Vec2((fxRand() - 0.5f) * 0.5f, (fxRand() - 0.5f) * 0.5f);
                            spawnParticle(hitDrawn, drift, 0.9f + fxRand() * 0.6f, 3.5f + fxRand() * 2.5f, smoke, 3);
                        }
                    }
                    ++app.cutBites;
                }
            }
        }
    } else if (app.hoverOre >= 0) {
        // Mining out a clump of ore the cutter uncovered, a piece at a time:
        // the beam works the next piece until it comes loose and flies to
        // the player, then moves on to the one after.
        Planet::Ore& o = p.ores[app.hoverOre];
        app.laserOre = app.hoverOre;
        int had = Deposits::taken(o);
        app.laserEnd = Deposits::pieceCenter(p, o, had);
        app.laserDepth = -6.0f;
        app.laserHit = true;
        o.hp -= mineDt / Deposits::toughness(o.kind);
        int got = Deposits::taken(o);
        float col[3];
        Deposits::color(p, o.kind, col);
        const float pale[3] = {0.5f + 0.5f * col[0], 0.5f + 0.5f * col[1], 0.5f + 0.5f * col[2]};
        int item = resourceItem(p, Deposits::yield(o).resource);
        for (int k = had; k < got; k++) {
            addItem(item, 1);
            Vec2 at = Deposits::pieceCenter(p, o, k);
            // The piece itself, a few chips of it, and a flash where it was.
            for (int i = 0; i < 4; i++) {
                float ang = fxRand() * 360.0f, sp = i == 0 ? 1.0f + fxRand() : 1.5f + fxRand() * 3.0f;
                spawnParticle(at, Vec2(cosd(ang) * sp, sind(ang) * sp), 0.9f + fxRand() * 0.5f,
                              i == 0 ? o.size * 0.38f : 1.6f + fxRand() * 1.4f, col, 1);
            }
            for (int i = 0; i < 6; i++) {
                float ang = fxRand() * 360.0f, sp = 2.5f + fxRand() * 4.0f;
                spawnParticle(at, Vec2(cosd(ang) * sp, sind(ang) * sp), 0.16f + fxRand() * 0.2f,
                              1.6f + fxRand() * 1.6f, pale, 0);
            }
        }
        if (o.hp <= 0.0f) {
            // The last piece is out: what is left of the lump crumbles...
            float g[3];
            p.groundTone(Deposits::center(p, o), g);
            const float chip[3] = {g[0] * 0.8f, g[1] * 0.8f, g[2] * 0.8f};
            float rad = Deposits::radius(o);
            for (int i = 0; i < 9; i++) {
                float ang = fxRand() * 360.0f, sp = 0.8f + fxRand() * 2.2f;
                Vec2 off(cosd(ang) * rad * 0.7f * fxRand(), sind(ang) * rad * 0.7f * fxRand());
                spawnParticle(Deposits::center(p, o) + off, Vec2(cosd(ang) * sp, sind(ang) * sp),
                              0.45f + fxRand() * 0.45f, 1.8f + fxRand() * 2.0f, chip, 2);
            }
            // ...and its pocket is dug out of the rock, which shows the next
            // clump of the vein. (That can add to p.ores: `o` is not used past here.)
            size_t known = p.ores.size();
            uploadCarveRegion(Deposits::extract(p, app.laserOre));
            oreFound(p, known);
            app.laserOre = -1;
        }
    } else if (app.hoverProp >= 0) {
        // Mining something in a cave.
        Planet::CaveProp& q = p.caveProps[app.hoverProp];
        app.laserProp = app.hoverProp;
        app.laserEnd = CaveField::center(p, q);
        app.laserDepth = q.front ? 10.0f : -6.0f;
        app.laserHit = true;
        q.hp -= mineDt / CaveField::toughness(q.kind);
        if (q.hp <= 0.0f) {
            NearField::Yield y = CaveField::yield(q);
            addItem(resourceItem(p, y.resource), y.amount);
            float col[3];
            CaveField::color(p, q.kind, col);
            int bits = 5 + y.amount * 2;
            for (int i = 0; i < bits; i++) {
                float ang = fxRand() * 360.0f, sp = 1.5f + fxRand() * 3.5f;
                Vec2 off(cosd(ang) * CaveField::radius(q) * 0.6f * fxRand(),
                         sind(ang) * CaveField::radius(q) * 0.6f * fxRand());
                spawnParticle(app.laserEnd + off, Vec2(cosd(ang) * sp, sind(ang) * sp),
                              0.9f + fxRand() * 0.5f, 2.2f + fxRand() * 1.8f, col, 1);
            }
            CaveField::mine(q);
            app.laserProp = -1;
        }
    } else if (app.hoverTarget >= 0) {
        // Cutting something.
        Planet::Harvestable& h = p.harvest[app.hoverTarget];
        app.laserTarget = app.hoverTarget;
        app.laserEnd = NearField::center(p, h);
        app.laserDepth = h.depth;
        app.laserHit = true;
        h.hp -= mineDt / NearField::toughness(h.kind);
        if (h.hp <= 0.0f) {
            NearField::Yield y = NearField::yield(h);
            addItem(resourceItem(p, y.resource), y.amount);
            const float* col = NearField::color(p, h.kind);
            int bits = 5 + y.amount * 2;
            for (int i = 0; i < bits; i++) {
                float ang = fxRand() * 360.0f, sp = 1.5f + fxRand() * 3.5f;
                Vec2 off(cosd(ang) * NearField::radius(h) * 0.6f * fxRand(),
                         sind(ang) * NearField::radius(h) * 0.6f * fxRand());
                spawnParticle(app.laserEnd + off, Vec2(cosd(ang) * sp, sind(ang) * sp),
                              0.9f + fxRand() * 0.5f, 2.2f + fxRand() * 1.8f, col, 1);
            }
            NearField::harvest(h);
            app.laserTarget = -1;
        }
    } else {
        // Nothing under the cursor: the beam goes where you point, up to its
        // range.
        if (app.inHole >= 0.5f) {
            // Underground it stops at the first rock on the way there — the
            // wall of the tunnel or cave you are in. (As for the cutter: what
            // the cursor points at is WALK_DROP above where it is drawn.)
            Vec2 upv = (a.pos - p.center).normalize();
            Vec2 drop = upv * NearField::WALK_DROP;
            Vec2 eye = a.pos + upv * 16.0f;
            Vec2 d = (cursor + drop) - eye;
            float len = d.length();
            float reach = len < LASER_RANGE ? len : LASER_RANGE;
            app.laserEnd = (len > 1e-3f ? eye + d * (reach / len) : eye) - drop;
            app.laserHit = false;
            app.laserDepth = 0.0f;
            for (float t = 6.0f; t <= reach; t += 3.0f) {
                Vec2 q = eye + d * (t / len);
                if (p.rockAt(q) > 0.5f) {
                    app.laserEnd = q - drop;
                    app.laserHit = true;
                    break;
                }
            }
        } else {
            // On the surface, below the terrain line it is hitting the ground
            // (in front of or behind the walking line — that's what the depth
            // is for).
            Vec2 d = cursor - a.pos;
            float len = d.length();
            if (len > LASER_RANGE) d = d * (LASER_RANGE / len);
            app.laserEnd = a.pos + d;
            float ang = atan2d(app.laserEnd.y - p.center.y, app.laserEnd.x - p.center.x);
            float drop = p.surfaceRadius(ang) - dist(app.laserEnd.x, app.laserEnd.y, p.center.x, p.center.y);
            app.laserHit = drop > 0.0f;
            float depth = NearField::depthAtDrop(drop);
            app.laserDepth = !app.laserHit ? 0.0f : (depth < -20.0f ? -20.0f : (depth > 80.0f ? 80.0f : depth));
        }
    }
    a.aimWorld = app.laserEnd;
    // Face whatever you're shooting at.
    Vec2 up = (a.pos - p.center).normalize();
    float side = (app.laserEnd.x - a.pos.x) * -up.y + (app.laserEnd.y - a.pos.y) * up.x;
    if (fabsf(side) > 3.0f) a.facing = side > 0.0f ? 1 : -1;
    // Sparks where the beam lands.
    if (app.laserHit) {
        for (int i = 0; i < 2; i++) {
            float ang = fxRand() * 360.0f, sp = 2.0f + fxRand() * 4.0f;
            spawnParticle(app.laserEnd, Vec2(cosd(ang) * sp, sind(ang) * sp),
                          0.18f + fxRand() * 0.25f, 1.4f + fxRand() * 1.2f, sparkCol, 0);
        }
    }
}

static void updateParticles(float dt, float timeScale) {
    Vec2 home = playerPos() - dominantPlanet().center;       // where harvested bits fly to
    home = home + (playerPos() - dominantPlanet().center).normalize() * 4.0f;
    for (size_t i = 0; i < app.particles.size();) {
        App::Particle& q = app.particles[i];
        q.life -= dt;
        bool done = q.life <= 0.0f;
        if (q.mode == 1) {
            // Burst outward for a moment, then home in on the player, faster
            // the longer it has been flying.
            float age = q.maxLife - q.life;
            Vec2 to = home - q.local;
            float d = to.length();
            if (age > 0.16f) {
                float pull = 0.5f + age * 6.0f;
                Vec2 want = d > 0.01f ? to * ((2.0f + age * 16.0f) / d) : Vec2(0, 0);
                float k = pull * 0.12f * timeScale;
                if (k > 1.0f) k = 1.0f;
                q.vel = q.vel + (want - q.vel) * k;
            } else {
                q.vel = q.vel * (1.0f - 0.08f * timeScale);
            }
            if (age > 0.16f && d < 9.0f) done = true;       // collected
        } else if (q.mode == 2) {
            // Ember: thrown out, then falls.
            Vec2 down = q.local.normalize() * -1.0f;
            q.vel = q.vel * (1.0f - 0.03f * timeScale) + down * (0.07f * timeScale);
        } else if (q.mode == 3) {
            // Smoke: drifts up, slows, spreads.
            Vec2 upw = q.local.normalize();
            q.vel = q.vel * (1.0f - 0.05f * timeScale) + upw * (0.012f * timeScale);
            q.size += 0.06f * timeScale;
        } else if (q.mode == 4) {
            // Water droplet: thrown up, falls back under the world's own
            // gravity, and is gone when it meets the water again.
            const Planet& wp = dominantPlanet();
            float r = q.local.length();
            Vec2 down = q.local * (-1.0f / (r > 1.0f ? r : 1.0f));
            float g = wp.gravity / (r > 1.0f ? r * r : 1.0f);
            q.vel = q.vel * (1.0f - 0.012f * timeScale) + down * (g * timeScale);
            if (q.maxLife - q.life > 0.12f && wp.sea && r < drawnWaterline(wp) - 1.0f) done = true;
        } else if (q.mode == 5) {
            // Bubble: wobbles upward, gone at the surface.
            const Planet& wp = dominantPlanet();
            float r = q.local.length();
            Vec2 upw = q.local * (1.0f / (r > 1.0f ? r : 1.0f));
            Vec2 side(-upw.y, upw.x);
            float wob = sinf(q.life * 9.0f + q.size * 7.0f) * 0.035f;
            q.vel = q.vel * (1.0f - 0.07f * timeScale) + upw * (0.05f * timeScale) + side * (wob * timeScale);
            if (!wp.sea || r > drawnWaterline(wp) - 1.5f) done = true;
        } else if (q.mode == 6) {
            // Ripple: a ring that spreads and fades where it started.
            q.size += 0.20f * timeScale;
            q.vel = Vec2(0, 0);
        } else {
            q.vel = q.vel * (1.0f - 0.10f * timeScale);
        }
        q.local = q.local + q.vel * timeScale;
        if (done) { q = app.particles.back(); app.particles.pop_back(); }
        else i++;
    }
}

// What the water does around the ship and the astronaut each frame: a
// floating ship rides the swell and sheds rings; exhaust blown at the water
// kicks up spray; someone going into it makes a splash, wading or treading
// water leaves rings, and under it their helmet (and a running jetpack)
// lets bubbles go.
static void updateWaterFx(float dt, float time) {
    Lander& l = app.lander;
    const Planet& p = dominantPlanet();
    l.bob = Vec2(0, 0);
    if (!p.sea) { app.wasWet = false; return; }

    Vec2 shipUp = (l.pos - p.center).normalize();
    float shipAng = atan2d(shipUp.y, shipUp.x);
    bool shipOverWater = p.overWater(shipAng);
    if (app.state == LANDED && shipOverWater && app.stuckIdx == app.dominantIdx) {
        // Afloat: a slow rise and fall (drawn only — where the ship IS does
        // not move), and now and then a ring off the hull.
        l.bob = shipUp * (1.0f * sinf(time * 0.52f) + 0.45f * sinf(time * 0.31f + 1.3f));
        app.shipRippleIn -= dt;
        if (app.shipRippleIn <= 0.0f) {
            app.shipRippleIn = 1.5f + fxRand() * 1.4f;
            spawnRipple(p, l.pos + Vec2(-shipUp.y, shipUp.x) * ((fxRand() - 0.5f) * 16.0f), 9.0f + fxRand() * 5.0f);
        }
    }
    if (app.state == PLAYING && l.active && l.thrusting > 0.0f && l.fuel > 0.0f && shipOverWater) {
        // Exhaust on the water: spray fanned up and out from under the
        // nozzle, harder the closer the ship is.
        const float REACH = 150.0f;
        Vec2 jet = l.noseDir() * -1.0f;                 // the way the exhaust goes
        float downward = -jet.dot(shipUp);              // 1 = straight at the water
        float height = dist(l.pos.x, l.pos.y, p.center.x, p.center.y) - p.seaRadius();
        if (downward > 0.3f && height > 0.0f && height < REACH) {
            float near = (1.0f - height / REACH) * l.thrusting;
            Vec2 side(-shipUp.y, shipUp.x);
            Vec2 hit = l.pos + jet * (height / downward);
            Vec2 where = p.center + (hit - p.center).normalize() * (drawnWaterline(p) + 1.0f);
            float col[3];
            sprayColor(p, where, col);
            float n = near * 3.2f * dt * 60.0f;
            int count = (int)n + (fxRand() < n - floorf(n) ? 1 : 0);
            for (int i = 0; i < count; i++) {
                float fan = (fxRand() - 0.5f) * 2.4f, lift = 0.5f + fxRand() * 0.8f;
                float sp = (1.6f + fxRand() * 3.2f) * (0.5f + near);
                spawnParticle(where + side * ((fxRand() - 0.5f) * 22.0f), (shipUp * lift + side * fan) * sp,
                              0.45f + fxRand() * 0.5f, 1.2f + fxRand() * 1.8f, col, 4);
            }
            if (fxRand() < near * 0.10f * dt * 60.0f) spawnRipple(p, hit, 8.0f + fxRand() * 8.0f);
        }
    }

    if (!app.spacewalking) { app.wasWet = false; return; }
    const Astronaut& a = app.astronaut;
    Vec2 up = (a.pos - p.center).normalize();
    Vec2 side(-up.y, up.x);
    bool wetNow = a.wet > 0.0f;
    float falling = -a.vel.dot(up);
    if (wetNow && !app.wasWet && falling > 0.9f && p.overWater(atan2d(up.y, up.x)))
        splashDown(p, a.pos, a.vel, 9.0f);
    app.wasWet = wetNow;
    if (!wetNow) return;
    float speed = a.vel.length();
    // At the surface (in to the shins, head still out) and moving: rings.
    bool atSurface = a.chestDepth < 9.0f && p.overWater(atan2d(up.y, up.x));
    app.rippleIn -= dt * (0.35f + speed);
    if (atSurface && app.rippleIn <= 0.0f) {
        app.rippleIn = 0.55f + fxRand() * 0.3f;
        spawnRipple(p, a.pos + side * ((fxRand() - 0.5f) * 8.0f), 5.0f + fxRand() * 4.0f);
    }
    // Helmet under: bubbles, from where the helmet is drawn.
    app.bubbleIn -= dt;
    if (a.chestDepth > 15.0f && app.bubbleIn <= 0.0f) {
        app.bubbleIn = 0.45f + fxRand() * 0.9f;
        Vec2 helmet = a.pos + up * (Astronaut::HEAD_UP - NearField::WALK_DROP) + side * (5.0f * a.facing);
        int n = 1 + (int)(fxRand() * 2.4f);
        for (int i = 0; i < n; i++)
            spawnBubble(p, helmet + side * ((fxRand() - 0.5f) * 5.0f) + up * (fxRand() * 4.0f), 1.0f + fxRand() * 1.5f);
    }
    // A jetpack run under water churns out a stream of them.
    if (a.jetting && a.chestDepth > 2.0f && fxRand() < 0.55f * dt * 60.0f) {
        Vec2 nozzle = a.pos + up * (2.0f - NearField::WALK_DROP) - side * (6.0f * a.facing);
        spawnBubble(p, nozzle + side * ((fxRand() - 0.5f) * 4.0f), 1.3f + fxRand() * 2.0f);
    }
}

// Beam, impact glow, particles and the "you can shoot this" brackets. World
// space, inside an active batch, drawn over the near field's front items.
static void drawLaserFx(float time) {
    Renderer2D& r = app.renderer;
    const Planet& p = dominantPlanet();
    float px = 1.0f / app.view.scale;

    // Brackets around the harvestable under the cursor.
    bool overPlant = app.hoverTarget >= 0 && app.hoverTarget < (int)p.harvest.size();
    bool overProp = app.hoverProp >= 0 && app.hoverProp < (int)p.caveProps.size();
    bool overOre = app.hoverOre >= 0 && app.hoverOre < (int)p.ores.size();
    if (app.spacewalking && (overPlant || overProp || overOre)) {
        Vec2 c;
        float rad;
        if (overOre) {
            c = Deposits::center(p, p.ores[app.hoverOre]);
            rad = Deposits::radius(p.ores[app.hoverOre]) + 3.0f * px;
        } else if (overProp) {
            c = CaveField::center(p, p.caveProps[app.hoverProp]);
            rad = CaveField::radius(p.caveProps[app.hoverProp]) + 3.0f * px;
        } else {
            const Planet::Harvestable& h = p.harvest[app.hoverTarget];
            c = NearField::center(p, h);
            rad = NearField::radius(h) + 3.0f * px;
        }
        Vec2 up = (c - p.center).normalize(), right(-up.y, up.x);
        float arm = rad * 0.45f, w = 1.5f * px;
        r.fill(1.0f, 1.0f, 1.0f, app.laserOn ? 0.95f : 0.65f);
        for (int sx = -1; sx <= 1; sx += 2) {
            for (int sy = -1; sy <= 1; sy += 2) {
                Vec2 corner = c + right * (rad * sx) + up * (rad * sy);
                Vec2 a1 = corner - right * (arm * sx), a2 = corner - up * (arm * sy);
                r.line(corner.x, corner.y, a1.x, a1.y, w);
                r.line(corner.x, corner.y, a2.x, a2.y, w);
            }
        }
    }

    r.setBlend(Renderer2D::Blend::Additive);
    if (app.spacewalking && app.laserOn) {
        // The beam widens toward things in front of the walking line and
        // narrows toward things behind it — it is travelling out of / into the
        // screen, and that taper is what says so.
        Vec2 a = app.astronaut.muzzleWorld, b = app.laserEnd;
        Vec2 d = b - a;
        float len = d.length();
        if (len > 0.5f) {
            Vec2 dir = d * (1.0f / len), n(-dir.y, dir.x);
            float flick = 0.85f + 0.15f * sinf(time * 31.0f);
            float w0 = 2.6f, w1 = 2.6f * NearField::scale(app.laserDepth) * (app.laserDepth < 0 ? 0.75f : 1.25f);
            auto band = [&](float k, float cr, float cg, float cb, float ca) {
                Renderer2D::Color col{cr, cg, cb, ca * flick};
                Vec2 a0 = a + n * (w0 * k), a1 = a - n * (w0 * k);
                Vec2 b0 = b + n * (w1 * k), b1 = b - n * (w1 * k);
                r.triangleV(a0, col, b0, col, b1, col);
                r.triangleV(a0, col, b1, col, a1, col);
            };
            // Mining laser: hot orange. Terrain cutter: the ship's teal.
            const bool cut = app.laserCutting;
            const float g0[3] = {cut ? 0.20f : 1.0f, cut ? 0.85f : 0.36f, cut ? 0.80f : 0.16f};
            const float g1[3] = {cut ? 0.35f : 1.0f, cut ? 0.95f : 0.55f, cut ? 0.88f : 0.25f};
            const float core[3] = {cut ? 0.85f : 1.0f, cut ? 1.0f : 0.95f, cut ? 0.97f : 0.85f};
            band(3.2f, g0[0], g0[1], g0[2], 0.16f);                 // outer glow
            band(1.6f, g1[0], g1[1], g1[2], 0.38f);
            band(0.55f, core[0], core[1], core[2], 0.95f);          // bright core
            // Muzzle flash + impact.
            r.fill(g1[0], g1[1], g1[2], 0.45f * flick);
            r.circle(a.x, a.y, 4.5f, 14);
            if (app.laserHit) {
                // (Kept small on ore: the piece under the beam is the show.)
                float ir = (cut ? 5.0f : (app.laserOre >= 0 ? 4.0f : 7.0f)) * NearField::scale(app.laserDepth) * flick;
                r.fill(g0[0], g0[1], g0[2], cut ? 0.22f : 0.30f);
                r.circle(b.x, b.y, ir * 1.9f, 18);
                r.fill(core[0], core[1], core[2], 0.75f);
                r.circle(b.x, b.y, ir * 0.7f, 14);
            }
        }
    }
    // Sparks and embers glow (additive)...
    for (const App::Particle& q : app.particles) {
        if (q.mode != 0 && q.mode != 2) continue;
        float t = q.life / q.maxLife;
        Vec2 w = p.center + q.local;
        r.fill(q.r, q.g, q.b, t);
        r.circle(w.x, w.y, q.size * (0.4f + 0.6f * t), 8);
    }
    r.setBlend(Renderer2D::Blend::Normal);
    // ...smoke is soft grey, thinning as it spreads...
    for (const App::Particle& q : app.particles) {
        if (q.mode != 3) continue;
        float t = q.life / q.maxLife;
        Vec2 w = p.center + q.local;
        r.fill(q.r, q.g, q.b, 0.17f * t);
        r.circle(w.x, w.y, q.size, 12);
    }
    // ...water thrown up in drops, bubbles going up through it, and rings
    // spreading on it: flat and pale. A ring is an ellipse lying on the
    // surface — the ground-ellipse rule, flatter, as water is seen at a lower
    // angle across it than the ground at your feet — only the outline.
    for (const App::Particle& q : app.particles) {
        if (q.mode < 4 || q.mode > 6) continue;
        float t = q.life / q.maxLife;
        Vec2 w = p.center + q.local;
        if (q.mode == 4) {
            r.fill(q.r, q.g, q.b, t < 0.5f ? 0.3f + 1.2f * t : 0.9f);
            r.circle(w.x, w.y, q.size * (0.55f + 0.45f * t), 8);
        } else if (q.mode == 5) {
            float grow = 0.7f + 0.3f * (1.0f - t);
            r.fill(q.r, q.g, q.b, 0.30f);
            r.circle(w.x, w.y, q.size * grow, 10);
            r.fill(1.0f, 1.0f, 1.0f, 0.55f);
            r.circle(w.x - q.size * 0.3f * grow, w.y - q.size * 0.3f * grow, q.size * 0.32f * grow, 6);
        } else {
            Vec2 up = q.local.normalize(), right(-up.y, up.x);
            float rx = q.size, ry = q.size * 0.40f;
            const int SEG = 22;
            r.fill(q.r, q.g, q.b, 0.55f * t);
            Vec2 prev = w + right * rx;
            for (int i = 1; i <= SEG; i++) {
                float a = (float)i / SEG * 6.2831853f;
                Vec2 next = w + right * (cosf(a) * rx) + up * (sinf(a) * ry);
                r.line(prev.x, prev.y, next.x, next.y, 1.1f * px + 0.3f);
                prev = next;
            }
        }
    }
    // ...harvested bits are solid chips of what was cut, in its own color, so
    // you can see WHAT you just picked up as it flies to you: a facet lit
    // from above and a shaded one under it, like everything else cut from
    // the ground.
    for (const App::Particle& q : app.particles) {
        if (q.mode != 1) continue;
        Vec2 w = p.center + q.local;
        Vec2 up = q.local.normalize(), right(-up.y, up.x);
        float s = q.size * 1.2f;
        Vec2 l = w - right * s, rt = w + right * s, top = w + up * (s * 0.85f), bot = w - up * (s * 0.85f);
        r.fill(q.r * 0.58f, q.g * 0.58f, q.b * 0.58f, 1.0f);
        r.triangle(l.x, l.y, bot.x, bot.y, rt.x, rt.y);
        r.fill(q.r, q.g, q.b, 1.0f);
        r.triangle(l.x, l.y, top.x, top.y, rt.x, rt.y);
    }
}

static void drawCloudsPass(float t) {
    const int N = SHADER_MAX_PLANETS;
    float pos[N * 2] = {0}, outerR[N] = {0}, cover[N] = {0};
    float lit[N * 3] = {0}, dark[N * 3] = {0};
    int count = (int)app.planets.size();
    if (count > N) count = N;
    for (int i = 0; i < count; i++) {
        const Planet& p = app.planets[i];
        pos[i * 2] = p.center.x; pos[i * 2 + 1] = p.center.y;
        if (p.isSun || !p.atmosphere || p.look.cloudCover <= 0.0f) { outerR[i] = 0.0f; continue; }  // no deck
        float mountainTop = p.maxSurfaceRadius();
        outerR[i] = mountainTop + (p.atmosphereOuterRadius() - mountainTop) * gDebug.cloudHeight;
        cover[i] = p.look.cloudCover;
        for (int k = 0; k < 3; k++) {
            lit[i * 3 + k] = p.look.cloudLit[k];
            dark[i * 3 + k] = p.look.cloudDark[k];
        }
    }

    GLuint pr = app.cloudProgram;
    glUseProgram(pr);
    setPassViewUniforms(pr);
    glUniform1f(glGetUniformLocation(pr, "u_time"), t);
    glUniform1i(glGetUniformLocation(pr, "u_planetCount"), count);
    glUniform2fv(glGetUniformLocation(pr, "u_planetPos"), N, pos);
    glUniform1fv(glGetUniformLocation(pr, "u_planetOuterR"), N, outerR);
    glUniform1fv(glGetUniformLocation(pr, "u_cover"), N, cover);
    glUniform3fv(glGetUniformLocation(pr, "u_cloudLit"), N, lit);
    glUniform3fv(glGetUniformLocation(pr, "u_cloudDark"), N, dark);
    setLightUniforms(pr);
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
static bool drawBodyPass(BodyPass pass) {
    // The veil run only has something to draw where holes have been dug; the
    // cave mask, wherever there is a carve window (natural caves live in it).
    if (pass != BodyPass::Main) {
        if (app.activeFieldIdx < 0) return false;
        const Planet& ap = app.planets[app.activeFieldIdx];
        if (!ap.carve.ready()) return false;
        if (pass == BodyPass::Veil && !ap.hasDigs()) return false;
    }
    const int N = SHADER_MAX_PLANETS;
    float pos[N * 2] = {0}, maxR[N], baseR[N] = {0}, noiseBand[N] = {0};
    float tones[N * 5 * 3] = {0};
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
        for (int k = 0; k < 5; k++)
            for (int ch = 0; ch < 3; ch++)
                tones[(i * 5 + k) * 3 + ch] = p.look.tones[k][ch];
    }

    GLuint pr = app.bodyProgram;
    glUseProgram(pr);
    setPassViewUniforms(pr);
    glUniform1i(glGetUniformLocation(pr, "u_planetCount"), count);
    glUniform2fv(glGetUniformLocation(pr, "u_planetPos"), N, pos);
    glUniform1fv(glGetUniformLocation(pr, "u_planetMaxR"), N, maxR);
    glUniform1fv(glGetUniformLocation(pr, "u_baseRadius"), N, baseR);
    glUniform1fv(glGetUniformLocation(pr, "u_noiseBand"), N, noiseBand);
    glUniform3fv(glGetUniformLocation(pr, "u_tone"), N * 5, tones);
    setLightUniforms(pr);
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
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, app.carveTex);
    glUniform1i(glGetUniformLocation(pr, "u_carveTex"), 2);
    glActiveTexture(GL_TEXTURE0);   // leave unit 0 active for the other passes
    {
        // The carve window is stored planet-local; hand the shader its world
        // position this frame (the planet has moved along its orbit).
        const Planet* ap = app.activeFieldIdx >= 0 ? &app.planets[app.activeFieldIdx] : nullptr;
        bool carved = ap && ap->carve.ready();
        glUniform1f(glGetUniformLocation(pr, "u_carveSize"), carved ? (float)CarveMap::N : 0.0f);
        // The front strip runs from the walking line to where the nearest
        // plants stand (NearField owns those numbers).
        glUniform1f(glGetUniformLocation(pr, "u_walkDrop"), NearField::WALK_DROP);
        glUniform1f(glGetUniformLocation(pr, "u_mouthRise"), NearField::MOUTH_RISE);
        glUniform1f(glGetUniformLocation(pr, "u_mouthRound"), NearField::MOUTH_ROUND);
        glUniform1i(glGetUniformLocation(pr, "u_veilPass"),
                    pass == BodyPass::Veil ? 1 : (pass == BodyPass::CaveMask ? 2 : 0));
        // The front strip starts just under a hole's mouth and runs to where
        // the nearest plants stand.
        glUniform1f(glGetUniformLocation(pr, "u_veilTop"), NearField::WALK_DROP + NearField::MOUTH);
        glUniform1f(glGetUniformLocation(pr, "u_veilBottom"), NearField::groundDrop(NearField::FRONT_DEPTH));
        glUniform1f(glGetUniformLocation(pr, "u_veilAlpha"), gDebug.frontVeil);
        glUniform1f(glGetUniformLocation(pr, "u_holeFloor"), gDebug.holeFloor);
        glUniform1f(glGetUniformLocation(pr, "u_holeDepth"), gDebug.holeDepth);
        Vec2 pp = playerPos();
        glUniform2f(glGetUniformLocation(pr, "u_playerPos"), pp.x, pp.y);
        glUniform1f(glGetUniformLocation(pr, "u_playerIn"), app.inHole);
        // Hot bites, at their world positions this frame.
        float burn[App::MAX_BURNS * 4] = {0};
        int nb = 0;
        if (ap) {
            for (const App::Burn& b : app.burns) {
                if (b.heat <= 0.0f || nb >= App::MAX_BURNS) continue;
                burn[nb * 4 + 0] = ap->center.x + b.local.x;
                burn[nb * 4 + 1] = ap->center.y + b.local.y;
                burn[nb * 4 + 2] = b.heat;
                burn[nb * 4 + 3] = b.radius;
                nb++;
            }
        }
        glUniform4fv(glGetUniformLocation(pr, "u_burn"), App::MAX_BURNS, burn);
        glUniform1i(glGetUniformLocation(pr, "u_burnCount"), nb);

        // Cave lights: the player's lamp first (only down a hole), then the
        // glowing props nearest the camera.
        const int MAX_GLOW = 14;
        float glow[MAX_GLOW * 4] = {0}, glowCol[MAX_GLOW * 3] = {0};
        int ng = 0;
        bool here = ap && app.dominantIdx == app.activeFieldIdx;
        if (here && app.spacewalking && app.inHole > 0.01f) {
            Vec2 lamp = app.astronaut.pos + (app.astronaut.pos - ap->center).normalize() * 18.0f;
            glow[0] = lamp.x; glow[1] = lamp.y; glow[2] = 135.0f;
            glowCol[0] = 0.20f * app.inHole; glowCol[1] = 0.185f * app.inHole; glowCol[2] = 0.155f * app.inHole;
            ng = 1;
        }
        if (here && carved) {
            CaveField::Light lights[MAX_GLOW];
            int n = CaveField::lights(*ap, caveFrame(0.0f), lights, MAX_GLOW - ng);
            for (int k = 0; k < n; k++, ng++) {
                glow[ng * 4 + 0] = lights[k].pos.x; glow[ng * 4 + 1] = lights[k].pos.y;
                glow[ng * 4 + 2] = lights[k].radius;
                glowCol[ng * 3 + 0] = lights[k].r; glowCol[ng * 3 + 1] = lights[k].g; glowCol[ng * 3 + 2] = lights[k].b;
            }
        }
        glUniform4fv(glGetUniformLocation(pr, "u_glow"), MAX_GLOW, glow);
        glUniform3fv(glGetUniformLocation(pr, "u_glowCol"), MAX_GLOW, glowCol);
        glUniform1i(glGetUniformLocation(pr, "u_glowCount"), ng);

        // Natural cave mouths near the view.
        const int MAX_MOUTHS = 8;
        float mouths[MAX_MOUTHS * 2] = {0};
        int nm = 0;
        if (ap) {
            float va = atan2d(app.view.focusY - ap->center.y, app.view.focusX - ap->center.x);
            float span = 0.5f * sqrtf((float)app.width * app.width + (float)app.height * app.height) /
                         app.view.scale / (ap->baseRadius > 1.0f ? ap->baseRadius : 1.0f) * RAD2DEG * 1.2f;
            for (const Planet::CaveMouth& cm : ap->caveMouths) {
                float da = fabsf(fmodf(cm.angle - va + 540.0f, 360.0f) - 180.0f);
                if (da > span + cm.half || nm >= MAX_MOUTHS) continue;
                mouths[nm * 2] = fmodf(cm.angle + 360.0f, 360.0f);
                mouths[nm * 2 + 1] = cm.half;
                nm++;
            }
        }
        glUniform2fv(glGetUniformLocation(pr, "u_natMouth"), MAX_MOUTHS, mouths);
        glUniform1i(glGetUniformLocation(pr, "u_natMouthCount"), nm);
        glUniform1f(glGetUniformLocation(pr, "u_carveCell"), CarveMap::CELL);
        glUniform2f(glGetUniformLocation(pr, "u_carveOrigin"),
                    carved ? ap->center.x + ap->carve.originX() : 0.0f,
                    carved ? ap->center.y + ap->carve.originY() : 0.0f);
    }
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
    return true;
}

// Water pass (see kWaterFrag). mode 0: the water itself, laid down before the
// body pass so the crust is painted over it. mode 1: the water in front of the
// walking line, over the ship and the astronaut, and the water standing in
// flooded holes. mode 2: the water over the lake bed in front of the walking
// line. The last two only for the world you are at, and only up close.
static void drawWaterPass(int mode, float t) {
    const int N = SHADER_MAX_PLANETS;
    int count = (int)app.planets.size();
    if (count > N) count = N;
    bool any = false;
    for (int i = 0; i < count; i++) any = any || app.planets[i].sea;
    if (!any) return;
    const Planet* ap = app.activeFieldIdx >= 0 ? &app.planets[app.activeFieldIdx] : nullptr;
    if (mode != 0 && (!ap || !ap->sea || app.view.scale < 0.08f)) return;

    float pos[N * 2] = {0}, seaR[N] = {0};
    float water[N * 3] = {0}, deep[N * 3] = {0}, top[N * 3] = {0};
    for (int i = 0; i < count; i++) {
        const Planet& p = app.planets[i];
        pos[i * 2] = p.center.x; pos[i * 2 + 1] = p.center.y;
        if (!p.sea) continue;
        seaR[i] = p.seaRadius();
        for (int k = 0; k < 3; k++) {
            water[i * 3 + k] = p.look.water[k];
            deep[i * 3 + k] = p.look.waterDeep[k];
            // The far surface is seen at a glancing angle: it takes on the
            // pale of the sky down at the horizon.
            float v = (p.look.water[k] * 0.56f + p.look.haze[k] * 0.44f) * 1.05f;
            top[i * 3 + k] = v > 1.0f ? 1.0f : v;
        }
    }

    GLuint pr = app.waterProgram;
    glUseProgram(pr);
    setPassViewUniforms(pr);
    glUniform1i(glGetUniformLocation(pr, "u_planetCount"), count);
    glUniform2fv(glGetUniformLocation(pr, "u_planetPos"), N, pos);
    glUniform1fv(glGetUniformLocation(pr, "u_seaR"), N, seaR);
    glUniform3fv(glGetUniformLocation(pr, "u_water"), N, water);
    glUniform3fv(glGetUniformLocation(pr, "u_waterDeep"), N, deep);
    glUniform3fv(glGetUniformLocation(pr, "u_waterTop"), N, top);
    setLightUniforms(pr);
    Vec2 sunW = sunWorldPos();
    glUniform2f(glGetUniformLocation(pr, "u_sunPos"), sunW.x, sunW.y);
    glUniform1f(glGetUniformLocation(pr, "u_time"), t);
    glUniform1i(glGetUniformLocation(pr, "u_mode"), mode);
    glUniform1i(glGetUniformLocation(pr, "u_activeField"), app.activeFieldIdx);
    glUniform1f(glGetUniformLocation(pr, "u_walkDrop"), NearField::WALK_DROP);
    glUniform1f(glGetUniformLocation(pr, "u_stripBottom"), NearField::groundDrop(NearField::FRONT_DEPTH));
    glUniform1f(glGetUniformLocation(pr, "u_tint"), gDebug.waterTint);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, app.terrainTex);
    glUniform1i(glGetUniformLocation(pr, "u_terrainTex"), 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, app.fieldTex);
    glUniform1i(glGetUniformLocation(pr, "u_fieldTex"), 1);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, app.carveTex);
    glUniform1i(glGetUniformLocation(pr, "u_carveTex"), 2);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, app.wetTex);
    glUniform1i(glGetUniformLocation(pr, "u_wetTex"), 3);
    glActiveTexture(GL_TEXTURE0);
    bool carved = ap && ap->carve.ready();
    glUniform1f(glGetUniformLocation(pr, "u_carveSize"), carved ? (float)CarveMap::N : 0.0f);
    glUniform1f(glGetUniformLocation(pr, "u_carveCell"), CarveMap::CELL);
    glUniform2f(glGetUniformLocation(pr, "u_carveOrigin"),
                carved ? ap->center.x + ap->carve.originX() : 0.0f,
                carved ? ap->center.y + ap->carve.originY() : 0.0f);
    if (ap) {
        const TerrainField& f = ap->field;
        glUniform2f(glGetUniformLocation(pr, "u_fieldOrigin"), f.originX(), f.originY());
        glUniform1f(glGetUniformLocation(pr, "u_fieldCell"), f.cellSize());
        glUniform2f(glGetUniformLocation(pr, "u_fieldSize"), (float)f.cornersX(), (float)f.cornersY());
    } else {
        glUniform1f(glGetUniformLocation(pr, "u_fieldCell"), 250.0f);
        glUniform2f(glGetUniformLocation(pr, "u_fieldSize"), 1.0f, 1.0f);
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);   // premultiplied "over"
    glBindVertexArray(app.fsVao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

// Cave formations grow out of the wall: they are clipped to the cave's open
// space so the rock hides their roots (the flat bases of the spikes). One
// stencil bit, above the ones the ridge layers count in (drawRidges), marks
// that space for the frame; the frame's clear wipes it.
static const GLuint CAVE_STENCIL_BIT = 0x80;
static bool gCaveMasked = false;    // whether this frame's mask was drawn

static void drawCaveMask() {
    glEnable(GL_STENCIL_TEST);
    glStencilMask(CAVE_STENCIL_BIT);
    glStencilFunc(GL_ALWAYS, CAVE_STENCIL_BIT, CAVE_STENCIL_BIT);
    glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    gCaveMasked = drawBodyPass(BodyPass::CaveMask);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glStencilMask(0xFF);
    glDisable(GL_STENCIL_TEST);
}

// Bracket drawing with these to clip it to the cave's open space. Without a
// mask this frame (no carve window) nothing is clipped.
static void beginInCave() {
    if (!gCaveMasked) return;
    glEnable(GL_STENCIL_TEST);
    glStencilFunc(GL_EQUAL, CAVE_STENCIL_BIT, CAVE_STENCIL_BIT);
    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
}
static void endInCave() { glDisable(GL_STENCIL_TEST); }

// One atmosphere disk (radius = atmosphere top), drawn twice per frame:
// mode 0 = the scattering sky band above the terrain (before the ridges, so
// distant ranges paint over the sky and the terrain paints over them — true
// painter's-order occlusion); mode 1 = the altitude-gated face fill below the
// terrain line (after the body, so the from-space sky sphere tints the rock).
static void drawAtmospherePass(float t, int mode) {
    const int N = SHADER_MAX_PLANETS;
    float pos[N * 2] = {0}, prad[N] = {0}, arad[N] = {0}, gain[N] = {0};
    float betaR[N * 3] = {0}, betaM[N * 3] = {0}, haze[N * 3] = {0};
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
        gain[i] = p.look.skyGain;
        for (int k = 0; k < 3; k++) {
            betaR[i * 3 + k] = p.look.skyBeta[k];
            betaM[i * 3 + k] = p.look.mieBeta[k];
            haze[i * 3 + k] = p.look.haze[k];
        }
    }

    GLuint pr = app.atmoProgram;
    glUseProgram(pr);
    setPassViewUniforms(pr);
    glUniform1f(glGetUniformLocation(pr, "u_time"), t);
    glUniform1i(glGetUniformLocation(pr, "u_planetCount"), count);
    glUniform2fv(glGetUniformLocation(pr, "u_planetPos"), N, pos);
    glUniform1fv(glGetUniformLocation(pr, "u_planetRadius"), N, prad);
    glUniform1fv(glGetUniformLocation(pr, "u_atmoRadius"), N, arad);
    glUniform3fv(glGetUniformLocation(pr, "u_betaR"), N, betaR);
    glUniform3fv(glGetUniformLocation(pr, "u_betaM"), N, betaM);
    glUniform1fv(glGetUniformLocation(pr, "u_skyGain"), N, gain);
    glUniform3fv(glGetUniformLocation(pr, "u_hazeCol"), N, haze);
    setLightUniforms(pr);
    glUniform1f(glGetUniformLocation(pr, "u_horizonHaze"), gDebug.horizonHaze);
    glUniform1i(glGetUniformLocation(pr, "u_hazeIdx"), app.dominantIdx);
    glUniform1f(glGetUniformLocation(pr, "u_hazeRef"), app.camGround);
    // About a third of the visible height.
    glUniform1f(glGetUniformLocation(pr, "u_hazeView"),
                0.30f * app.height / (app.view.scale > 1e-6f ? app.view.scale : 1e-6f));
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

static void syncDebugFromJS();   // defined below

static void init() {
    app.renderer.init();
    initShaderPasses();

    // ---- Generate the solar system ----------------------------------------
    // One seed -> star, planets, moons, orbits, looks, names (system_gen.cpp).
    // The host page picks the seed (?seed= in the URL, else the one remembered
    // in localStorage, else random) and publishes it as window.GAME_SEED.
    unsigned int seed = (unsigned int)EM_ASM_INT({
        var s = (window.GAME_SEED >>> 0) % 2147483647;
        return s > 0 ? s : 1;
    });
    app.system = generateSystem(seed, app.planets);
    app.sunIdx = app.system.sunIdx;
    app.homeIdx = app.system.homeIdx;
    app.dominantIdx = app.stuckIdx = app.homeIdx;
    homePlanet().visited = true;
    printf("=== System %s (seed %u): %d bodies ===\n", app.system.starName.c_str(), seed,
           (int)app.planets.size());
    for (int i = 0; i < (int)app.planets.size(); i++) {
        const Planet& p = app.planets[i];
        printf("  [%d] %-10s %-16s r=%6.0f orbit=%8.0f parent=%d g=%.3f\n", i, p.name.c_str(),
               p.kind.c_str(), p.baseRadius, p.orbitRadius, p.orbitParent,
               p.gravity / (p.baseRadius * p.baseRadius));
    }

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

    // Hand the body list to the debug panel (its "Warp to" buttons).
    {
        std::string list;
        for (int i = 0; i < (int)app.planets.size(); i++) {
            const Planet& p = app.planets[i];
            list += std::to_string(i) + "," + p.name + "," + p.kind + "," + (p.isSun ? "1" : "0") + ";";
        }
        EM_ASM({ if (window.setBodies) window.setBodies(UTF8ToString($0)); }, list.c_str());
    }

    Input::init("#canvas");
    // Pull the debug panel's values in BEFORE the first reset, so the run
    // starts with them (a customised Max Fuel used to start at the built-in
    // 250 and only fill up after the first landing).
    syncDebugFromJS();
    resetGame();

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    app.startTime = emscripten_get_now() / 1000.0;
    app.lastFrameTime = app.startTime;

    printf("=== Cow Abductor (C++/WebGL) ===\n");
    printf("W/Up - thrust | A/D or Left/Right - rotate | M - system map\n");
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
    // On the ground these run; in the air they steer (see Astronaut::move).
    if (left)  a.walkLeft(p, timeScale);
    if (right) a.walkRight(p, timeScale);

    bool jumpKey = Input::isKeyDown(Input::KEY_W) || Input::isKeyDown(Input::KEY_UP);
    if (jumpKey && a.grounded) a.jump(p);
    // In water the same key swims up (and holds you at the surface); S dives.
    else if (jumpKey) a.swim(p, timeScale, +1);
    if (Input::isKeyDown(Input::KEY_S) || Input::isKeyDown(Input::KEY_DOWN)) a.swim(p, timeScale, -1);

    a.jetting = false;
    if (Input::isKeyDown(Input::KEY_SPACE)) a.jetpack(p, timeScale);

}

// One step for the herd; a cow that reaches the hatch is cargo.
static void updateCows(float timeScale) {
    for (Cow& cow : app.cows) {
        if (cow.update(homePlanet(), app.lander, app.cargo, maxCargo(), timeScale)) {
            app.cargo += 1;
            app.score += 50;
        }
    }
}

// Decide each frame whether each loose cow is inside the active beam cone and
// pull it in (ported from lander.js abduct()). Returns nothing; cows transition
// their own state and report stowing back through update().
static void runBeamGrab() {
    Lander& l = app.lander;
    bool canGrab = l.beamOn && app.cargo < maxCargo();
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
    const Planet& body = dominantPlanet();

    Vec2 focusPos = playerPos();
    // The astronaut simulates in the body's co-moving frame, so their world
    // velocity is that plus the body's orbital motion.
    Vec2 focusVel = app.spacewalking ? app.astronaut.vel + body.orbitVel : app.lander.vel;

    // Altitude above the ground under the player — the true terrain, averaged
    // over a ~1400-unit stretch so flying across a jagged range doesn't pump
    // the zoom. (Not the spherical base radius: that put a ship parked on a
    // mountain "high up", and made airless bodies — which have no atmosphere
    // band to interpolate across — zoom wildly on touchdown.)
    float d = dist(focusPos.x, focusPos.y, body.center.x, body.center.y);
    float ang = atan2d(focusPos.y - body.center.y, focusPos.x - body.center.x);
    float stepDeg = (700.0f / 3.0f) / (body.baseRadius > 1.0f ? body.baseRadius : 1.0f) * RAD2DEG;
    float ground = 0.0f;
    // (Over water the surface is the ground: afloat is not "high up".)
    for (int k = -3; k <= 3; k++) ground += body.visibleRadius(ang + k * stepDeg);
    ground /= 7.0f;
    float alt = d - ground;
    if (alt < 0) alt = 0;
    app.camAlt += (alt - app.camAlt) * 0.12f * timeScale;
    app.camGround = ground;

    // One framing rule from the ground to deep space: hold the ground zoom
    // while low, then pull back just fast enough that the surface stays
    // `cameraFraming` screen-heights below the ship. The horizon never leaves
    // the frame on the way up or down, and you watch it curve as you climb.
    float gz = gDebug.cameraGroundZoom;
    float framingPx = app.height * gDebug.cameraFraming;
    float targetScale = framingPx / (alt > 1.0f ? alt : 1.0f);
    if (targetScale > gz) targetScale = gz;
    // Speed look-ahead: never so close that the next `cameraSpeedLook` seconds
    // of travel (relative to the world you're at) run off the screen. Standing
    // or walking leaves the ground zoom alone; a ship coming in fast, or
    // skimming the surface, gets a wider view and closes in as it slows.
    if (gDebug.cameraSpeedLook > 0.0f) {
        float relSpeed = (focusVel - body.orbitVel).length();       // world units / frame
        float travel = relSpeed * 60.0f * gDebug.cameraSpeedLook;   // world units in that time
        float speedScale = app.height / (travel > 1.0f ? travel : 1.0f);
        if (targetScale > speedScale) targetScale = speedScale;
    }
    // Deep-space floor: low enough that cruising between bodies keeps the
    // whole neighborhood in frame.
    if (targetScale < 0.0008f) targetScale = 0.0008f;
    targetScale *= v.userZoom;

    Vec2 target = focusPos;
    if (app.mapMode) {
        // System map: frame every orbit around the star.
        float extent = 1.0f;
        for (const Planet& p : app.planets) {
            float reach = dist(p.center.x, p.center.y, app.planets[app.sunIdx].center.x,
                               app.planets[app.sunIdx].center.y) + p.atmosphereOuterRadius();
            if (reach > extent) extent = reach;
        }
        float fit = (app.width < app.height ? app.width : app.height) * 0.46f;
        targetScale = fit / extent;
        target = app.planets[app.sunIdx].center;
        focusVel = Vec2(0, 0);
    } else {
        // Horizon on the lower third: near the ground the camera looks a
        // little above the ship, so the sky gets most of the frame and the
        // rock cross-section below the surface gets less. Eases back to
        // centered by the altitude where the zoom starts pulling out.
        float knee = framingPx / (gz > 1e-4f ? gz : 1e-4f);
        float drop = gDebug.cameraHorizon * (1.0f - smoothstepf(0.0f, knee, alt));
        Vec2 up = (focusPos - body.center).normalize();
        target = focusPos + up * (drop * app.height / (v.scale > 1e-6f ? v.scale : 1e-6f));
    }

    v.scale += (targetScale - v.scale) * View::ZOOM_EASE * timeScale;

    // Velocity-predictive follow so easing only corrects displacement: first
    // advance the focus by the target's own motion, THEN ease out whatever
    // offset is left. (Easing against the un-advanced focus, as this used to,
    // leaves the camera a full frame of motion ahead of the target forever —
    // invisible while worlds stood still, but a planet now travels ~60 units a
    // frame along its orbit, which put the player visibly off-center when
    // zoomed in.)
    v.focusX += focusVel.x * timeScale;
    v.focusY += focusVel.y * timeScale;
    v.focusX += (target.x - v.focusX) * View::FOLLOW_EASE * timeScale;
    v.focusY += (target.y - v.focusY) * View::FOLLOW_EASE * timeScale;

    // Roll so the dominant body's gravity points screen-down (single body, so
    // standing on a moon doesn't tilt toward the planet pulling from afar).
    // The system map levels out instead.
    float targetRot = v.rotation;
    Vec2 g = app.lander.gravityAt(dominantPlanet(), focusPos);
    if (app.mapMode) targetRot = 0.0f;
    else if (g.lengthSq() > 1e-12f) targetRot = 90.0f - atan2d(g.y, g.x);
    float delta = std::fmod(targetRot - v.rotation + 540.0f, 360.0f) - 180.0f;
    v.rotation += delta * View::ROLL_EASE * timeScale;

    // Nav overlay fades in once worlds have shrunk to specks.
    float navTarget = 1.0f - smoothstepf(0.0035f, 0.0070f, v.scale);
    app.navAlpha += (navTarget - app.navAlpha) * 0.15f * timeScale;
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

// Reference body for the trajectory aid: whichever body pulls hardest on the
// ship, the star included. Near a world the path is drawn as seen from that
// world (a moving planet's frame — the only frame in which "will I hit it?"
// reads directly); out in open space it is the star's frame.
static int trajectoryFrame() {
    int best = app.sunIdx;
    float bestG = -1.0f;
    for (int i = 0; i < (int)app.planets.size(); i++) {
        const Planet& pl = app.planets[i];
        float dx = pl.center.x - app.lander.pos.x, dy = pl.center.y - app.lander.pos.y;
        float distSq = dx * dx + dy * dy;
        if (distSq < 1.0f) distSq = 1.0f;
        float g = pl.gravity / distSq;
        if (g > bestG) { bestG = g; best = i; }
    }
    return best;
}

// Predicted coast path, drawn as a fading line that thins/fades along its
// length. Width is kept ~constant on screen by dividing by the view scale.
// If the path passes close to (or ends on) another world, a ghost ring marks
// where that world will be at that moment — it is moving too, so the place to
// aim is ahead of where it is now.
static void drawTrajectory() {
    Renderer2D& r = app.renderer;
    app.lander.predictTrajectory(app.planets, trajectoryFrame(), app.trajectory, app.encounter);
    const auto& pts = app.trajectory;
    int n = (int)pts.size();
    if (n < 2) return;
    float px = 1.0f / app.view.scale;
    float w = 1.6f * px;
    for (int i = 0; i + 1 < n; i++) {
        // Gentle fade so the far end of the arc stays visible (brightest at ship).
        float fade = 1.0f - 0.6f * ((float)i / n);
        r.fill(0.72f, 0.85f, 1.0f, 0.5f * fade);
        r.line(pts[i].x, pts[i].y, pts[i + 1].x, pts[i + 1].y, w);
    }

    const Lander::Encounter& e = app.encounter;
    if (e.body < 0 || e.body >= (int)app.planets.size()) return;
    const Planet& b = app.planets[e.body];
    // Only worth a ghost if the world will have MOVED by then; otherwise the
    // ring would just sit on top of the real thing.
    if (dist(e.bodyPos.x, e.bodyPos.y, b.center.x, b.center.y) < b.baseRadius * 0.75f) return;
    float ringR = b.baseRadius > 7.0f * px ? b.baseRadius : 7.0f * px;
    const int segs = 48;
    r.fill(b.look.sig[0], b.look.sig[1], b.look.sig[2], e.impact ? 0.85f : 0.55f);
    for (int i = 0; i < segs; i += 2) {   // dashed ring
        float a0 = (float)i / segs * 360.0f, a1 = (float)(i + 1) / segs * 360.0f;
        r.line(e.bodyPos.x + cosd(a0) * ringR, e.bodyPos.y + sind(a0) * ringR,
               e.bodyPos.x + cosd(a1) * ringR, e.bodyPos.y + sind(a1) * ringR, 1.6f * px);
    }
    if (!e.impact) {
        // Closest-approach tick on the path.
        r.fill(0.72f, 0.85f, 1.0f, 0.8f);
        r.circle(e.shipPos.x, e.shipPos.y, 3.2f * px, 12);
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
        r.fill(sun.look.sig[0], sun.look.sig[1], sun.look.sig[2], 14.0f / 255.0f);
        r.circle(sun.center.x, sun.center.y, rad, 48);
    }
    // Core: the star's color pushed most of the way to white-hot.
    r.fill(sun.look.sig[0] * 0.3f + 0.7f, sun.look.sig[1] * 0.3f + 0.7f,
           sun.look.sig[2] * 0.3f + 0.7f, 1.0f);
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
    float surfR = p.visibleRadius(ang);      // shadows fall on water as on ground
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
        // (Not for one that has dropped into a hole — same reason as the
        // astronaut's, below.)
        const Planet& hp = homePlanet();
        float below = hp.surfaceRadius(atan2d(cow.pos.y - hp.center.y, cow.pos.x - hp.center.x)) -
                      dist(cow.pos.x, cow.pos.y, hp.center.x, hp.center.y);
        if (below > Cow::STAND_UP) continue;
        drawBlobShadow(hp, cow.pos, 14.0f, 320.0f, toSunHome);
    }
    const Planet& dom = dominantPlanet();
    Vec2 toSunDom = (sunW - dom.center).normalize();
    drawBlobShadow(dom, app.lander.pos, app.lander.radius * 1.5f, 650.0f, toSunDom);
    if (app.spacewalking) {
        // Not when they are down a hole: the shadow is projected onto the
        // terrain's outline, which would put it on the ground above them.
        const Vec2 ap = app.astronaut.pos;
        float below = dom.surfaceRadius(atan2d(ap.y - dom.center.y, ap.x - dom.center.x)) -
                      dist(ap.x, ap.y, dom.center.x, dom.center.y);
        // Nor when they are in over the knees: no shadow on the water from
        // someone standing in it.
        if (below < 12.0f && app.astronaut.wet < 0.4f)
            drawBlobShadow(dom, ap, Astronaut::RADIUS * 1.7f, 320.0f, toSunDom);
    }
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
    float strength = surfaceDressing();
    if (strength <= 0.01f) return;

    Vec2 focus = playerPos();
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

// "Behind the mountains" clipping. The ridge layers are translucent on purpose
// — letting the sky tint them is what makes a range read as far away — but a
// mountain is still solid: nothing behind it may show through. drawRidges
// marks every pixel a ridge layer covers in the stencil buffer; whatever is
// drawn between these two calls (sky bodies, the far cloud band, shooting
// stars) is kept out of those pixels. The sky still shows through the layers,
// because it was drawn before them — only the things BEHIND them are cut.
static void beginBehindRidges() {
    glEnable(GL_STENCIL_TEST);
    glStencilFunc(GL_EQUAL, 0, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
}
static void endBehindRidges() { glDisable(GL_STENCIL_TEST); }

// The other worlds of the system, hanging in the sky. Seen from the ground the
// real bodies are far off-screen (the nearest is tens of screens away), so each
// is projected onto a sky dome around the player: its TRUE direction in the
// plane is kept, only the distance is compressed. A body straight overhead sits
// near the top of the frame; one near the horizon sits low to the side, and as
// it sets it slides behind the ridges and the terrain. Because the star goes on
// the same dome, sunrise and sunset happen where the lighting says they do: the
// disc touches the horizon exactly when the terminator reaches you.
//
// Size follows the real angular size (radius / distance), so a moon's parent
// planet looms and a far neighbor is a small disc.
//
// Only the SUNLIT part of a world is drawn — like the Moon in our own sky, the
// unlit part is simply not visible. The shape is a true phase: full when the
// world is opposite the star, a thin crescent when it is nearly between you
// and the star, with the bright limb pointing at the star's place in the sky.
// One flat tone (the art style's flat fields); no outline around the dark side.
//
// occluders = true is the pre-sky pass: each world's whole disc in black,
// drawn over the starfield before the sky goes on. Against black space that is
// invisible, but it stops stars shining through the unlit part of a crescent.
//
// Purely visual; fades out with the rest of the surface dressing as the real
// bodies come into view.
static void drawSkyBodies(const Mat3& viewMat, bool occluders = false) {
    const Planet& here = dominantPlanet();
    if (here.isSun) return;
    float strength = surfaceDressing();
    if (strength <= 0.01f) return;

    Vec2 focus = playerPos();
    Vec2 up = (focus - here.center).normalize();
    Vec2 sunW = sunWorldPos();
    Vec2 toSunHere = (sunW - here.center).normalize();
    // Daylight washes the worlds out; the star itself only shows by day.
    float day = here.atmosphere ? smoothstepf(-0.20f, 0.30f, up.dot(toSunHere)) : 0.0f;

    float W = (float)app.width, H = (float)app.height;
    Vec2 pc = viewMat.transform(focus);                 // player, clip space
    float rot = app.view.rotation;
    const float domeX = 0.44f * W, domeY = 0.52f * H;   // dome radii, px
    // Where a world direction lands on the dome, in px from the player (+y down).
    auto domePos = [&](Vec2 target) {
        float sAng = atan2d(target.y - focus.y, target.x - focus.x) + rot;
        return Vec2(cosd(sAng) * domeX, sind(sAng) * domeY);
    };
    Vec2 sunDome = domePos(sunW);

    // Far-to-near so a near body overlaps a far one.
    int order[SHADER_MAX_PLANETS + 6];
    float dists[SHADER_MAX_PLANETS + 6];
    int count = 0;
    for (int i = 0; i < (int)app.planets.size() && count < SHADER_MAX_PLANETS + 6; i++) {
        if (i == app.dominantIdx) continue;
        const Planet& b = app.planets[i];
        float dd = dist(focus.x, focus.y, b.center.x, b.center.y);
        int j = count++;
        while (j > 0 && dists[j - 1] < dd) { dists[j] = dists[j - 1]; order[j] = order[j - 1]; j--; }
        dists[j] = dd; order[j] = i;
    }

    // Own begin/end: a screen-space batch left pending is DISCARDED by the next
    // begin(), so every screen-space pass must flush itself.
    Renderer2D& r = app.renderer;
    r.begin(viewMat, app.width, app.height);
    r.beginScreenSpace();
    for (int oi = 0; oi < count; oi++) {
        const Planet& b = app.planets[order[oi]];
        float dd = dists[oi];
        if (dd < 1.0f) continue;
        Vec2 dp = domePos(b.center);
        float rpx = 0.7f * H * b.baseRadius / dd;
        if (b.isSun) rpx *= 0.45f;
        if (rpx < 2.5f) rpx = 2.5f;
        if (rpx > 0.22f * H) rpx = 0.22f * H;
        if (dp.y > rpx * 3.0f + 0.1f * H) continue;      // well below the horizon
        float cx = pc.x + dp.x * 2.0f / W, cy = pc.y - dp.y * 2.0f / H;
        float rx = rpx * 2.0f / W, ry = rpx * 2.0f / H;
        if (cx < -1.0f - rx * 3.0f || cx > 1.0f + rx * 3.0f) continue;

        const float* sig = b.look.sig;
        if (occluders) {
            if (b.isSun) continue;
            r.fill(0.0f, 0.0f, 0.0f, strength);
            r.ellipse(cx, cy, rx, ry, 36);
            continue;
        }
        if (b.isSun) {
            // Seen through more and more air as it sinks, the sun reddens and
            // its glow spreads: the same extinction that grades the light on
            // the ground. (No change on an airless world.)
            float sunS = up.dot(toSunHere);
            float low = here.atmosphere ? 1.0f - smoothstepf(0.0f, 0.40f, sunS) : 0.0f;
            LightParams LP = lightParams(here);
            float excess = std::fmax(1.0f / std::fmax(sunS * 0.9f + 0.1f, 0.05f) - 2.0f, 0.0f);
            float tint[3];
            for (int k = 0; k < 3; k++) tint[k] = expf(-LP.extK[k] * excess * 0.8f);
            // One smooth glow: a gradient fan from the disc out to nothing
            // (stacked translucent discs would read as concentric rings).
            r.setBlend(Renderer2D::Blend::Additive);
            const int segs = 40;
            const float glowK = 3.4f + 5.0f * low;       // glow radius, in core radii
            Renderer2D::Color hot{sig[0] * tint[0], sig[1] * tint[1], sig[2] * tint[2],
                                  (0.60f + 0.15f * low) * strength};
            Renderer2D::Color none{hot.r, hot.g, hot.b, 0.0f};
            Vec2 c(cx, cy);
            for (int k = 0; k < segs; k++) {
                float a0 = (float)k / segs * 6.2831853f, a1 = (float)(k + 1) / segs * 6.2831853f;
                Vec2 e0(cx + cosf(a0) * rx * glowK, cy + sinf(a0) * ry * glowK);
                Vec2 e1(cx + cosf(a1) * rx * glowK, cy + sinf(a1) * ry * glowK);
                r.triangleV(c, hot, e0, none, e1, none);
            }
            r.setBlend(Renderer2D::Blend::Normal);
            r.fill((sig[0] * 0.25f + 0.75f) * (0.35f + 0.65f * tint[0]),
                   (sig[1] * 0.25f + 0.75f) * (0.35f + 0.65f * tint[1]),
                   (sig[2] * 0.25f + 0.75f) * (0.35f + 0.65f * tint[2]), strength);
            r.ellipse(cx, cy, rx, ry, 36);
            continue;
        }

        // Phase. cosPhase = +1: the lit hemisphere faces us (full); -1: we are
        // looking at the night side (new — nothing to draw).
        Vec2 bodyToSun = (sunW - b.center).normalize();
        Vec2 bodyToUs = (focus - b.center).normalize();
        float cosPhase = bodyToSun.dot(bodyToUs);
        if (cosPhase < -0.985f) continue;
        // Bright limb toward the star's place on the dome (px, +y down).
        Vec2 limb(sunDome.x - dp.x, sunDome.y - dp.y);
        float ll = limb.length();
        limb = ll > 1.0f ? limb * (1.0f / ll) : Vec2(1.0f, 0.0f);
        Vec2 across(-limb.y, limb.x);

        // The lit region of a sphere, seen in projection: for every height
        // across the disc it runs from the terminator (a half-ellipse whose
        // width is cosPhase) out to the bright limb. Emitted as strips.
        float a = strength * (1.0f - 0.45f * day);
        r.fill(sig[0], sig[1], sig[2], a);
        auto toClip = [&](float alongPx, float acrossPx) {
            float ox = limb.x * alongPx + across.x * acrossPx;
            float oy = limb.y * alongPx + across.y * acrossPx;
            return Vec2(cx + ox * 2.0f / W, cy - oy * 2.0f / H);
        };
        const int segs = 24;
        for (int k = 0; k < segs; k++) {
            float s0 = -90.0f + 180.0f * (float)k / segs;
            float s1 = -90.0f + 180.0f * (float)(k + 1) / segs;
            float c0 = cosd(s0), c1 = cosd(s1), y0 = sind(s0) * rpx, y1 = sind(s1) * rpx;
            Vec2 o0 = toClip(rpx * c0, y0), o1 = toClip(rpx * c1, y1);                     // limb
            Vec2 t0 = toClip(-rpx * cosPhase * c0, y0), t1 = toClip(-rpx * cosPhase * c1, y1);  // terminator
            r.quad(o0.x, o0.y, o1.x, o1.y, t1.x, t1.y, t0.x, t0.y);
        }
    }
    r.endScreenSpace();
    r.end();
}

// A far, hazy cloud band high in the sky behind the ridges — a soft undulating
// ribbon at the top of the atmosphere that adds a whole plane between the flat
// sky and the farthest hills. Anchored to world angle (so flying reveals new
// puffs) but pinned overhead in radius (far things barely shift). Terminator-
// shaded and faded out toward space like the rest of the surface dressing.
static void drawFarClouds(const Mat3& viewMat, float time) {
    const Planet& p = dominantPlanet();
    if (p.isSun || !p.atmosphere || p.look.cloudCover <= 0.0f) return;
    float strength = surfaceDressing() * (p.look.cloudCover < 1.0f ? p.look.cloudCover : 1.0f);
    if (strength <= 0.01f) return;

    Vec2 focus = playerPos();
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
    // Lit like the main cloud layer: the graded day/dusk light, a little
    // ahead of the ground because the band stands high.
    LightParams LP = lightParams(p);
    auto lightAt = [&](float ang, float out[3]) {
        groundLight(LP, cosd(ang) * toSun.x + sind(ang) * toSun.y + 0.10f, out);
    };

    Renderer2D& r = app.renderer;
    r.begin(viewMat, app.width, app.height);
    for (int i = 0; i < seg; i++) {
        float a1 = viewAngle - span + 2.0f * span * ((float)i / seg);
        float a2 = viewAngle - span + 2.0f * span * ((float)(i + 1) / seg);
        float l1[3], l2[3];
        lightAt(a1, l1);
        lightAt(a2, l2);
        float p1 = puff(a1) * strength;
        float p2 = puff(a2) * strength;
        auto col = [&](float pa, const float l[3]) {
            // pale cloud in this world's cloud color, in that light
            return Renderer2D::Color{p.look.cloudLit[0] * 0.86f * l[0], p.look.cloudLit[1] * 0.88f * l[1],
                                     p.look.cloudLit[2] * 0.96f * l[2], pa * 0.55f};
        };
        Renderer2D::Color c1 = col(p1, l1), c2 = col(p2, l2);
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
    if (p.isSun || !p.atmosphere) return;                // shafts need air to scatter in
    float strength = surfaceDressing();
    if (strength <= 0.01f) return;

    Vec2 focus = playerPos();
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

    // Brightness along a beam follows the air it passes through: nothing above
    // the atmosphere (a beam is only visible where there is air to scatter
    // it — without this the upper ends showed as streaks against space once
    // the camera pulled back), strongest in the middle of the shell, dimmer
    // again toward the ground.
    auto airGlow = [&](Vec2 pt) {
        float h = (dist(pt.x, pt.y, p.center.x, p.center.y) - p.baseRadius) / thickness;
        return (1.0f - smoothstepf(0.50f, 0.92f, h)) * (0.18f + 0.82f * smoothstepf(-0.05f, 0.45f, h));
    };

    Renderer2D& r = app.renderer;
    r.begin(viewMat, app.width, app.height);
    r.setBlend(Renderer2D::Blend::Additive);
    const int N = 9;
    const int SEG = 6;
    for (int i = 0; i < N; i++) {
        float f = (float)i / (N - 1) - 0.5f;             // -0.5..0.5 across view
        float drift = sinf(time * 0.05f + i * 1.7f) * halfW * 0.09f;
        float off = f * (2.0f * halfW) + drift;
        Vec2 c(focus.x + perp.x * off, focus.y + perp.y * off);
        float w = thickness * 0.10f * (0.55f + 0.5f * sinf(time * 0.07f + i * 2.3f));
        if (w < thickness * 0.03f) w = thickness * 0.03f;
        float baseA = 0.06f * vis * (0.7f + 0.3f * sinf(time * 0.09f + i));
        Vec2 prev(c.x - d.x * len * 0.5f, c.y - d.y * len * 0.5f);   // sky end
        float prevA = baseA * airGlow(prev);
        for (int k = 1; k <= SEG; k++) {
            float t = (float)k / SEG - 0.5f;
            Vec2 cur(c.x + d.x * len * t, c.y + d.y * len * t);
            float curA = baseA * airGlow(cur);
            if (prevA > 0.0005f || curA > 0.0005f) {
                Renderer2D::Color c0{1.0f, 0.93f, 0.74f, prevA}, c1{1.0f, 0.93f, 0.74f, curA};
                Renderer2D::Color edge{1.0f, 0.93f, 0.74f, 0.0f};
                Vec2 p0m(prev.x - perp.x * w, prev.y - perp.y * w), p0p(prev.x + perp.x * w, prev.y + perp.y * w);
                Vec2 p1m(cur.x - perp.x * w, cur.y - perp.y * w), p1p(cur.x + perp.x * w, cur.y + perp.y * w);
                r.triangleV(p0m, edge, prev, c0, cur, c1);       // left half
                r.triangleV(p0m, edge, cur, c1, p1m, edge);
                r.triangleV(prev, c0, p0p, edge, p1p, edge);     // right half
                r.triangleV(prev, c0, p1p, edge, cur, c1);
            }
            prev = cur;
            prevA = curA;
        }
    }
    r.setBlend(Renderer2D::Blend::Normal);
    r.end();
}

// The distant haze layers of the world you're at, drawn behind the body so the
// terrain occludes everything below the horizon (painter's order).
//
// The layers are translucent on purpose: letting the sky show through is how a
// range gets its haze and reads as far away. But a mountain is solid, so the
// translucency must only ever reveal SKY — never a star, a planet, or the
// outline of another range behind it. Three things enforce that:
//   - starMask = true draws the silhouettes in opaque black right after the
//     starfield and BEFORE the sky. The backdrop there is black space, so this
//     erases exactly the stars (and the distant star's disc) behind the
//     mountains and changes nothing else.
//   - the color pass draws the layers nearest-first with a stencil test, so
//     each pixel is owned by the nearest range that covers it and blends with
//     the sky alone: one flat, hazed color per range, no farther range showing
//     through a nearer one.
//   - that same stencil marks "mountain here" for everything drawn behind the
//     ranges afterwards (see beginBehindRidges).
static void drawRidges(const Mat3& viewMat, bool starMask = false) {
    Renderer2D& r = app.renderer;
    const Planet& p = dominantPlanet();
    if (p.ridges.empty()) return;
    Vec2 focus = playerPos();
    Vec2 sunW = sunWorldPos();
    // Ridges are a surface-view dressing: fade them out on the climb to space
    // along with the face fill, so the limb doesn't grow a colored fringe.
    float strength = surfaceDressing();
    if (strength <= 0.01f) return;

    // The pale haze the layers dissolve into (per world; the home's is a warm
    // cream-blue, not pure blue) — the Summer Afternoon trick: distance reads
    // as light, not grey.
    const float hazeR = p.look.haze[0], hazeG = p.look.haze[1], hazeB = p.look.haze[2];

    r.begin(viewMat, app.width, app.height);
    float viewAngle = atan2d(focus.y - p.center.y, focus.x - p.center.x);
    Vec2 toSun = (sunW - p.center).normalize();
    LightParams LP = lightParams(p);
    // p.ridges is stored far-to-near; walk it backwards so the nearest layer
    // is emitted (and claims its pixels) first.
    for (int li = (int)p.ridges.size() - 1; li >= 0; li--) {
        const auto& layer = p.ridges[li];
        // Layer tint faded toward the sky haze by distance (skyMix).
        float cr = layer.tintR + (hazeR - layer.tintR) * layer.skyMix;
        float cg = layer.tintG + (hazeG - layer.tintG) * layer.skyMix;
        float cb = layer.tintB + (hazeB - layer.tintB) * layer.skyMix;
        float shift = viewAngle * layer.follow;

        // ONE flat color per layer (see docs/art-style.md): the layer tint
        // hazed by distance, darkened across the terminator, and hue-
        // shifted to ember inside the dusk band. No gradients, no rim.
        auto colorAt = [&](float angle) {
            // Mask pass: black, fading with the dressing so the stars come
            // back as the ridges fade out on the climb to space.
            if (starMask) return Renderer2D::Color{0.0f, 0.0f, 0.0f, strength};
            // The layer's flat tint in the graded day/dusk/night light — the
            // warm and rose phases of the terminator come from the light.
            float l[3];
            groundLightAt(p, LP, toSun, angle, l);
            return Renderer2D::Color{cr * l[0], cg * l[1], cb * l[2], layer.alpha * strength};
        };

        // Full unclipped bands: crest line down to a fixed skirt radius
        // well below any terrain. The body pass paints the rock over the
        // lower parts (painter's-order occlusion), so the visible color
        // field is continuous everywhere — no terrain reference, no seams,
        // and nothing shifts when the player (and the parallax) moves.
        // Only the stretch around the view is emitted.
        float skirtR = p.baseRadius * 0.85f;
        for (size_t i = 0; i + 1 < layer.points.size(); i++) {
            float a1 = layer.points[i].angle + shift;
            float a2 = layer.points[i + 1].angle + shift;
            float da = std::fmod(a1 - viewAngle + 540.0f, 360.0f) - 180.0f;
            if (fabsf(da) > 50.0f) continue;
            float rt1 = layer.points[i].r + layer.lift;
            float rt2 = layer.points[i + 1].r + layer.lift;
            Vec2 t1(p.center.x + cosd(a1) * rt1, p.center.y + sind(a1) * rt1);
            Vec2 t2(p.center.x + cosd(a2) * rt2, p.center.y + sind(a2) * rt2);
            Vec2 b1(p.center.x + cosd(a1) * skirtR, p.center.y + sind(a1) * skirtR);
            Vec2 b2(p.center.x + cosd(a2) * skirtR, p.center.y + sind(a2) * skirtR);
            Renderer2D::Color c1 = colorAt(a1);
            Renderer2D::Color c2 = colorAt(a2);
            r.triangleV(b1, c1, t1, c1, t2, c2);
            r.triangleV(b1, c1, t2, c2, b2, c2);
        }
    }
    if (starMask) {
        r.end();
        return;
    }
    // Color pass: draw only where no nearer layer has drawn yet, and mark the
    // pixel. Triangles within the one batch are rasterized in order, so the
    // nearest-first emission above is what makes "nearest wins" hold.
    glEnable(GL_STENCIL_TEST);
    glStencilMask(0xFF);
    glStencilFunc(GL_EQUAL, 0, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_INCR);
    r.end();
    glDisable(GL_STENCIL_TEST);
}

static float hash01(unsigned int n) {
    n = (n ^ 61u) ^ (n >> 16);
    n *= 9u;
    n ^= n >> 4;
    n *= 0x27d4eb2du;
    n ^= n >> 15;
    return (n & 0xffffffu) * (1.0f / 16777216.0f);
}

// The near framing planes: two dark silhouette bands along the bottom of the
// frame, closer to the camera than the action — the plane that passes in front
// of everything and shoves the scene behind it into depth.
//
// They are anchored to the SCREEN, not to the planet: the band always occupies
// the bottom edge and can never rise over the player, so nothing needs to be
// cut out of it to keep the ship visible. They still behave like near geometry:
//   - the silhouette is a fixed profile around the planet (noise sampled on a
//     circle, so it wraps), scrolled by the player's travel along the surface
//     times the layer's depth factor — nearer = faster;
//   - climbing makes them drop away faster than the ground does, so they are
//     gone within a few ship-lengths of lift-off;
//   - pulling the camera back shrinks them off the bottom of the frame.
// Spires and fronds stand on each band and sway. Flat color per layer,
// terminator-shaded, tinted from the world's own basin tone.
static void drawForeground(float time) {
    const Planet& p = dominantPlanet();
    if (p.isSun || gDebug.foreground <= 0.001f) return;
    float W = (float)app.width, H = (float)app.height;
    float zoomK = app.view.scale / (gDebug.cameraGroundZoom > 1e-4f ? gDebug.cameraGroundZoom : 1e-4f);
    if (zoomK < 0.25f) return;                     // pulled back: the near plane has left the frame
    if (zoomK > 1.5f) zoomK = 1.5f;
    float grow = smoothstepf(0.25f, 0.6f, zoomK) * zoomK * gDebug.foreground;
    // Never taller than the room under the player: the tallest band plus the
    // tallest prop on it is ~0.36 of the screen at grow = 1, and the player's
    // ground line sits (0.5 - cameraHorizon) above the bottom edge. Without
    // this cap, zooming in grew the band (and its fronds) up over the ship.
    float maxGrow = (0.5f - gDebug.cameraHorizon) / 0.36f;
    if (grow > maxGrow) grow = maxGrow;

    Vec2 focus(app.view.focusX, app.view.focusY);  // the camera point: already smoothed
    float theta = atan2f(focus.y - p.center.y, focus.x - p.center.x);
    float S = theta * p.baseRadius;                // travel along the surface
    float sinkBase = app.camAlt * app.view.scale / H;   // how far the ground has dropped, screen heights

    Vec2 up(cosf(theta), sinf(theta));
    Vec2 toSun = (sunWorldPos() - p.center).normalize();
    float light[3];
    groundLight(lightParams(p), up.dot(toSun), light);

    struct Layer { float depth, base, amp, feature, tone, propSpacing; unsigned int salt; };
    const Layer layers[2] = {
        {1.5f, 0.070f, 0.170f, 1500.0f, 1.00f, 760.0f, 11u},   // mid-dark plane
        {2.3f, 0.015f, 0.130f, 1100.0f, 0.55f, 520.0f, 29u},   // nearest: darkest, fastest
    };

    Renderer2D& r = app.renderer;
    r.begin(Mat3(), app.width, app.height);
    r.beginScreenSpace();
    for (const Layer& L : layers) {
        float sink = sinkBase * L.depth * 1.25f;
        float planePerPx = 1.0f / (app.view.scale * L.depth);   // plane units per screen px
        // The lump and prop spacings in the table are world units at a ground
        // zoom of 0.2. Rescale them to the actual ground zoom, so the band
        // shows the same number of hills and props across the screen whatever
        // that is set to (at a close zoom they would otherwise stretch into
        // one flat slope with a prop every few screens).
        float zoomRef = 0.2f / (gDebug.cameraGroundZoom > 1e-4f ? gDebug.cameraGroundZoom : 1e-4f);
        float feature = L.feature * zoomRef, propSpacing = L.propSpacing * zoomRef;
        float F = p.baseRadius / feature;                        // noise-circle radius
        float ox = (float)(p.seed % 97u) + L.salt * 7.3f;
        // Band height (fraction of screen height) at a screen x in px from center.
        auto heightAt = [&](float xpx) {
            float a = (S + xpx * planePerPx) / p.baseRadius;     // angle around the planet
            float n = app.fgNoise.noise(cosf(a) * F + ox, sinf(a) * F) * 0.75f +
                      app.fgNoise.noise(cosf(a) * F * 2.3f - ox, sinf(a) * F * 2.3f + 31.0f) * 0.25f;
            float v = smoothstepf(0.18f, 0.82f, 0.5f + 0.5f * n);
            return (L.base + L.amp * v) * grow - sink;
        };

        r.fill(p.look.fgTint[0] * L.tone * light[0], p.look.fgTint[1] * L.tone * light[1],
               p.look.fgTint[2] * L.tone * light[2], 1.0f);
        const int N = 80;
        float hPrev = heightAt(-W * 0.5f);
        for (int i = 0; i < N; i++) {
            float x0 = -1.0f + 2.0f * (float)i / N, x1 = -1.0f + 2.0f * (float)(i + 1) / N;
            float hNext = heightAt(x1 * W * 0.5f);
            if (hPrev > 0.0f || hNext > 0.0f) {
                float y0 = -1.0f + 2.0f * (hPrev > 0 ? hPrev : 0.0f);
                float y1 = -1.0f + 2.0f * (hNext > 0 ? hNext : 0.0f);
                r.quad(x0, -1.0f, x1, -1.0f, x1, y1, x0, y0);
            }
            hPrev = hNext;
        }

        // Props standing on the band, at fixed spots around the planet.
        if (p.look.propDensity <= 0.0f) continue;
        int perRev = (int)(6.2831853f * p.baseRadius / propSpacing);
        if (perRev < 8) perRev = 8;
        float spacing = 6.2831853f * p.baseRadius / perRev;
        float halfSpan = (W * 0.5f + 120.0f) * planePerPx;
        int k0 = (int)floorf((S - halfSpan) / spacing), k1 = (int)ceilf((S + halfSpan) / spacing);
        if (k1 - k0 > 60) continue;
        float keep = 0.55f * (p.look.propDensity < 1.5f ? p.look.propDensity : 1.5f);
        for (int k = k0; k <= k1; k++) {
            unsigned int idx = (unsigned int)(((k % perRev) + perRev) % perRev);
            unsigned int h = idx * 7919u + L.salt * 104729u + p.seed * 31u;
            if (hash01(h) > keep) continue;
            float c = ((float)k + (hash01(h + 1u) - 0.5f) * 0.6f) * spacing;
            float xpx = (c - S) / planePerPx;
            float hf = heightAt(xpx);
            if (hf <= 0.004f) continue;
            float size = H * (0.050f + 0.065f * hash01(h + 2u)) * grow * (L.depth / 2.3f + 0.35f);
            bool frond = hash01(h + 3u) < p.look.frondFrac;
            float sway = sinf(time * 0.6f + hash01(h + 4u) * 6.2831853f) * (frond ? 4.0f : 1.2f) * DEG2RAD;
            float baseX = xpx, baseY = (hf - 0.006f) * H;       // px; y up from the bottom edge
            // Triangle in prop-local px (x right, y up), rotated by `rotRad`
            // about the base point, emitted in clip space.
            auto tri = [&](float rotRad, float ax, float ay, float bx, float by, float cx2, float cy2) {
                float cs = cosf(rotRad), sn = sinf(rotRad);
                auto conv = [&](float lx, float ly) {
                    float rx = lx * cs - ly * sn, ry = lx * sn + ly * cs;
                    return Vec2((baseX + rx) * 2.0f / W, -1.0f + (baseY + ry) * 2.0f / H);
                };
                Vec2 A = conv(ax, ay), B = conv(bx, by), C = conv(cx2, cy2);
                r.triangle(A.x, A.y, B.x, B.y, C.x, C.y);
            };
            if (!frond) {
                tri(sway, -0.13f * size, 0, 0, size, 0.13f * size, 0);
                tri(sway, -0.24f * size, 0, -0.07f * size, 0.42f * size, 0.05f * size, 0);
            } else {
                for (int b = -2; b <= 2; b++) {
                    float bl = (1.0f - fabsf((float)b) * 0.16f) * size;
                    tri(sway + (float)b * 15.0f * DEG2RAD, -0.07f * size, 0, 0, bl, 0.07f * size, 0);
                }
            }
        }
    }
    r.endScreenSpace();
    r.end();
}

// Navigation overlay for the zoomed-out view: faint orbit rings, a marker disc
// for every world too small to see at this zoom, and a ring around the one you
// are flying around. Names are DOM labels (shell.html's #nav-labels), placed
// from here each frame. Undiscovered worlds are grey and unnamed.
static void drawNavOverlay(const Mat3& viewMat) {
    float a = app.navAlpha;
    if (a <= 0.02f) {
        if (app.navLabelsShown) {
            EM_ASM({ if (window.setNavLabels) window.setNavLabels(""); });
            app.navLabelsShown = false;
        }
        return;
    }
    Renderer2D& r = app.renderer;
    float px = 1.0f / app.view.scale;
    r.begin(viewMat, app.width, app.height);

    // Orbit rings.
    for (const Planet& b : app.planets) {
        if (b.orbitParent < 0 || b.orbitParent >= (int)app.planets.size()) continue;
        const Planet& par = app.planets[b.orbitParent];
        bool moon = !par.isSun;
        if (moon && b.orbitRadius * app.view.scale < 14.0f) continue;   // too tight to read
        const int segs = 160;
        r.fill(0.62f, 0.74f, 0.92f, (moon ? 0.10f : 0.16f) * a);
        for (int i = 0; i < segs; i++) {
            float a0 = (float)i / segs * 360.0f, a1 = (float)(i + 1) / segs * 360.0f;
            r.line(par.center.x + cosd(a0) * b.orbitRadius, par.center.y + sind(a0) * b.orbitRadius,
                   par.center.x + cosd(a1) * b.orbitRadius, par.center.y + sind(a1) * b.orbitRadius,
                   1.1f * px);
        }
    }

    // Markers + label list.
    app.labelBuf.clear();
    Vec2 ship = app.lander.pos;
    for (int i = 0; i < (int)app.planets.size(); i++) {
        const Planet& b = app.planets[i];
        bool moon = b.orbitParent >= 0 && !app.planets[b.orbitParent].isSun;
        float minR = (b.isSun ? 0.0f : (moon ? 3.0f : 4.5f)) * px;
        float shown = b.baseRadius > minR ? b.baseRadius : minR;
        if (!b.isSun && b.baseRadius < minR) {
            if (b.discovered) r.fill(b.look.sig[0], b.look.sig[1], b.look.sig[2], a);
            else              r.fill(0.50f, 0.52f, 0.56f, a);
            r.circle(b.center.x, b.center.y, minR, 20);
        }
        if (i == app.dominantIdx) {
            float ringR = (b.atmosphereOuterRadius() > shown ? b.atmosphereOuterRadius() : shown) + 5.0f * px;
            const int segs = 40;
            r.fill(0.45f, 1.0f, 0.86f, 0.55f * a);
            for (int k = 0; k < segs; k++) {
                float a0 = (float)k / segs * 360.0f, a1 = (float)(k + 1) / segs * 360.0f;
                r.line(b.center.x + cosd(a0) * ringR, b.center.y + sind(a0) * ringR,
                       b.center.x + cosd(a1) * ringR, b.center.y + sind(a1) * ringR, 1.2f * px);
            }
        }
        // A moon hugging its planet at this zoom would only stack labels.
        if (moon && b.orbitRadius * app.view.scale < 60.0f) continue;
        Vec2 clip = viewMat.transform(b.center);
        float sx = (clip.x * 0.5f + 0.5f) * app.width;
        float sy = (1.0f - (clip.y * 0.5f + 0.5f)) * app.height;
        // Worlds are labelled underneath; the star above (its corona is big,
        // and an inner planet's label would otherwise land on top of it).
        if (b.isSun) sy -= shown * 2.1f * app.view.scale + 34.0f;
        else         sy += shown * app.view.scale + 8.0f;
        if (sx < -40 || sx > app.width + 40 || sy < -20 || sy > app.height + 20) continue;
        float dd = dist(ship.x, ship.y, b.center.x, b.center.y) - b.baseRadius;
        char line[160];
        snprintf(line, sizeof(line), "%d,%d,%s,%s,%d,%d;", (int)sx, (int)sy,
                 b.discovered ? b.name.c_str() : "?",
                 b.discovered ? b.kind.c_str() : "Uncharted",
                 (int)(dd > 0 ? dd / 1000.0f : 0), i == app.dominantIdx ? 1 : 0);
        app.labelBuf += line;
    }
    r.end();

    EM_ASM({
        if (window.setNavLabels) window.setNavLabels(UTF8ToString($0), $1);
    }, app.labelBuf.c_str(), a);
    app.navLabelsShown = true;
}

// Cinematic depth vignette + near-ground grade: soft black gradient strips down
// the four screen edges, with the BOTTOM darkest so the nearest ground reads as
// closer and richer. Frames the whole scene and pushes the center forward. Pure
// screen space, drawn last over everything.
static void drawVignette() {
    // gDebug.vignette scales the whole effect (1 = the strip alphas below).
    float k = gDebug.vignette;
    if (k <= 0.001f) return;
    Renderer2D& r = app.renderer;
    r.begin(Mat3(), app.width, app.height);   // own batch, or the strips are never flushed
    r.beginScreenSpace();
    auto strip = [&](Vec2 e0, Vec2 e1, Vec2 i1, Vec2 i0, float a) {
        Renderer2D::Color edge{0, 0, 0, a * k}, in{0, 0, 0, 0};
        r.triangleV(e0, edge, e1, edge, i1, in);
        r.triangleV(e0, edge, i1, in, i0, in);
    };
    // bottom (darkest — nearest ground), top, left, right
    strip(Vec2(-1, -1), Vec2(1, -1), Vec2(1, -0.42f), Vec2(-1, -0.42f), 0.46f);
    strip(Vec2(-1, 1),  Vec2(1, 1),  Vec2(1, 0.60f),  Vec2(-1, 0.60f),  0.20f);
    strip(Vec2(-1, -1), Vec2(-1, 1), Vec2(-0.62f, 1), Vec2(-0.62f, -1), 0.24f);
    strip(Vec2(1, -1),  Vec2(1, 1),  Vec2(0.62f, 1),  Vec2(0.62f, -1),  0.24f);
    r.endScreenSpace();
    r.end();
}

// Drifting dust/spore motes at parallax depth planes — the cheap volume cue.
// Each mote's screen position is its base clip pos shifted by the camera's world
// position (scaled by the mote's depth, so near planes slide more) plus a slow
// sway, wrapped to stay on screen. Soft additive specks, faded out toward space.
static void drawMotes(float time) {
    float strength = surfaceDressing() * gDebug.motes;   // off unless the knob is raised
    if (strength <= 0.01f) return;
    // Position relative to the body: the planet itself is travelling along its
    // orbit, and world coordinates would smear the motes across the screen.
    Vec2 focus = playerPos() - dominantPlanet().center;
    // Motes are sunlit dust: they follow the daylight where you stand and are
    // gone at night (where pale specks drifting over the land would read as
    // stars in front of the mountains).
    {
        Vec2 toSun = (sunWorldPos() - dominantPlanet().center).normalize();
        Vec2 up = focus.normalize();
        strength *= smoothstepf(-gDebug.nightSoftness, gDebug.nightSoftness, up.dot(toSun));
        if (strength <= 0.01f) return;
    }
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
        ss.spawnFocus = app.astronaut.pos - dominantPlanet().center;
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
    Vec2 focus = playerPos() - dominantPlanet().center;
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
    // Newer knobs: fall back to the built-in default when the page predates them.
    gDebug.fuelBurn         = EM_ASM_DOUBLE({ return window.DBG.fuelBurn !== undefined ? window.DBG.fuelBurn : $0; }, gDebug.fuelBurn);
    gDebug.refuelRate       = EM_ASM_DOUBLE({ return window.DBG.refuelRate !== undefined ? window.DBG.refuelRate : $0; }, gDebug.refuelRate);
    gDebug.cameraHorizon    = EM_ASM_DOUBLE({ return window.DBG.cameraHorizon !== undefined ? window.DBG.cameraHorizon : $0; }, gDebug.cameraHorizon);
    gDebug.horizonHaze      = EM_ASM_DOUBLE({ return window.DBG.horizonHaze !== undefined ? window.DBG.horizonHaze : $0; }, gDebug.horizonHaze);
    gDebug.foreground       = EM_ASM_DOUBLE({ return window.DBG.foreground !== undefined ? window.DBG.foreground : $0; }, gDebug.foreground);
    gDebug.vignette         = EM_ASM_DOUBLE({ return window.DBG.vignette !== undefined ? window.DBG.vignette : $0; }, gDebug.vignette);
    gDebug.motes            = EM_ASM_DOUBLE({ return window.DBG.motes !== undefined ? window.DBG.motes : $0; }, gDebug.motes);
    gDebug.frontVeil        = EM_ASM_DOUBLE({ return window.DBG.frontVeil !== undefined ? window.DBG.frontVeil : $0; }, gDebug.frontVeil);
    gDebug.waterLevel       = EM_ASM_DOUBLE({ return window.DBG.waterLevel !== undefined ? window.DBG.waterLevel : $0; }, gDebug.waterLevel);
    gDebug.waterTint        = EM_ASM_DOUBLE({ return window.DBG.waterTint !== undefined ? window.DBG.waterTint : $0; }, gDebug.waterTint);
    gDebug.holeFloor        = EM_ASM_DOUBLE({ return window.DBG.holeFloor !== undefined ? window.DBG.holeFloor : $0; }, gDebug.holeFloor);
    gDebug.holeDepth        = EM_ASM_DOUBLE({ return window.DBG.holeDepth !== undefined ? window.DBG.holeDepth : $0; }, gDebug.holeDepth);
    gDebug.oreRichness      = EM_ASM_DOUBLE({ return window.DBG.oreRichness !== undefined ? window.DBG.oreRichness : $0; }, gDebug.oreRichness);
    gDebug.oreBurn          = EM_ASM_DOUBLE({ return window.DBG.oreBurn !== undefined ? window.DBG.oreBurn : $0; }, gDebug.oreBurn);
    gDebug.cameraSpeedLook  = EM_ASM_DOUBLE({ return window.DBG.cameraSpeedLook !== undefined ? window.DBG.cameraSpeedLook : $0; }, gDebug.cameraSpeedLook);
    gDebug.walkSpeed        = EM_ASM_DOUBLE({ return window.DBG.walkSpeed !== undefined ? window.DBG.walkSpeed : $0; }, gDebug.walkSpeed);
    gDebug.jumpSpeed        = EM_ASM_DOUBLE({ return window.DBG.jumpSpeed !== undefined ? window.DBG.jumpSpeed : $0; }, gDebug.jumpSpeed);

    // The gravity knob scales every body together (each keeps its own relative
    // density; the star keeps its mass ratio), then the orbit rates are
    // re-derived so planets still share orbits with ships coasting beside them.
    float dens = EM_ASM_DOUBLE({ return window.DBG.planetDensity; });
    if (dens != gDebug.planetDensity) {
        gDebug.planetDensity = dens;
        for (Planet& p : app.planets) p.applyGlobalDensity(dens);
        updateKeplerSpeeds(app.planets);
    }
    if (app.lander.fuel > maxFuel()) app.lander.fuel = maxFuel();
}

// Put the ship above a point of a world's surface (angle around the body,
// degrees) with a full tank, co-moving with it.
static void warpAbove(int idx, float angle, float altitude) {
    if (idx < 0 || idx >= (int)app.planets.size() || app.planets[idx].isSun) return;
    Planet& p = app.planets[idx];
    float r = p.visibleRadius(angle) + altitude;     // above the ground, or the water over it
    Lander& l = app.lander;
    l.reset();
    l.pos = p.center + Vec2(cosd(angle), sind(angle)) * r;
    l.vel = p.orbitVel;
    l.rotation = l.targetRotation = angle + 90.0f;
    app.state = PLAYING;
    app.spacewalking = false;
    app.astronaut.active = false;
    app.dominantIdx = app.stuckIdx = idx;
    app.mapMode = false;
    app.camAlt = altitude;
    app.view.focusX = l.pos.x;
    app.view.focusY = l.pos.y;
    app.view.scale = std::fmin(gDebug.cameraGroundZoom, app.height * gDebug.cameraFraming / altitude);
    Vec2 g = l.gravityAt(p, l.pos);
    app.view.rotation = 90.0f - atan2d(g.y, g.x);
}

// Dev aid (debug panel "Warp to"): drop the ship above a world's day side.
// Lets you tour a generated system without flying every leg. Exported to JS as
// Module._debugWarpTo(index).
extern "C" EMSCRIPTEN_KEEPALIVE void debugWarpTo(int idx) {
    if (idx < 0 || idx >= (int)app.planets.size() || app.planets[idx].isSun) return;
    const Planet& p = app.planets[idx];
    Vec2 toSun = (sunWorldPos() - p.center).normalize();
    warpAbove(idx, atan2d(toSun.y, toSun.x) + 30.0f, 1800.0f);     // mid-morning light
}

// Dev hook: the same, above a chosen point of the surface (angle around the
// body, degrees) at a chosen altitude — for looking at one particular piece of
// terrain. Module._debugWarpAngle(index, angle, altitude).
extern "C" EMSCRIPTEN_KEEPALIVE void debugWarpAngle(int idx, float angle, float altitude) {
    warpAbove(idx, angle, altitude < 20.0f ? 20.0f : altitude);
}

// Dev hook: put the astronaut (on foot) beside one of the dominant world's
// cave props — for looking at / testing a particular part of a cave. Returns
// how many props there are (so a test can walk the list); index < 0 just asks.
// Module._debugTeleportProp(index).
extern "C" EMSCRIPTEN_KEEPALIVE int debugTeleportProp(int idx) {
    Planet& p = dominantPlanet();
    int n = (int)p.caveProps.size();
    if (idx < 0 || idx >= n) return n;
    const Planet::CaveProp& q = p.caveProps[idx];
    Vec2 at = p.center + q.local + Vec2(cosd(q.dir), sind(q.dir)) * 20.0f;
    app.astronaut.spawn(at);
    app.spacewalking = true;
    return n;
}
// Dev hook: put the astronaut (on foot) on the surface of the dominant world
// at an angle (degrees). Module._debugTeleportAngle(angle).
extern "C" EMSCRIPTEN_KEEPALIVE void debugTeleportAngle(float angle) {
    Planet& p = dominantPlanet();
    float r = p.surfaceRadius(angle) + 6.0f;
    app.astronaut.spawn(Vec2(p.center.x + cosd(angle) * r, p.center.y + sind(angle) * r));
    app.spacewalking = true;
}
// Dev hook: the nearest shore to the player on the world they are at — the
// angle (degrees) where the ground meets the water, looking one way round
// (dir > 0), the other (dir < 0) or both (0). `into` moves the answer that
// many world units on into the water (negative: back onto the land). Returns
// -1000 on a dry world. Module._debugFindWater(dir, into).
extern "C" EMSCRIPTEN_KEEPALIVE float debugFindWater(int dir, float into) {
    const Planet& p = dominantPlanet();
    if (!p.sea) return -1000.0f;
    Vec2 pp = playerPos();
    float a0 = atan2d(pp.y - p.center.y, pp.x - p.center.x);
    float degPerUnit = 360.0f / (6.2831853f * p.baseRadius);
    float step = 10.0f * degPerUnit;
    bool startWet = p.overWater(a0);
    for (float off = step; off < 360.0f; off += step) {
        for (int sgn = -1; sgn <= 1; sgn += 2) {
            if (dir != 0 && sgn != (dir > 0 ? 1 : -1)) continue;
            float a = a0 + sgn * off;
            if (p.overWater(a) != startWet) {
                // `into` always heads into the water, whichever side we came from.
                float toWater = startWet ? -sgn : sgn;
                return a + toWater * into * degPerUnit;
            }
        }
    }
    return -1000.0f;
}
// Dev hook: put the ship down, parked, at an angle (degrees) on the dominant
// world — on the ground, or afloat if that is water. Module._debugParkAt(angle).
extern "C" EMSCRIPTEN_KEEPALIVE void debugParkAt(float angle) {
    Planet& p = dominantPlanet();
    float r = p.visibleRadius(angle) + 6.0f;
    app.lander.reset();
    app.lander.pos = Vec2(p.center.x + cosd(angle) * r, p.center.y + sind(angle) * r);
    app.lander.rotation = app.lander.targetRotation = angle + 90.0f;
    app.lander.vel = p.orbitVel;
    app.lander.land(p);
    app.stuckIdx = app.dominantIdx;
    app.state = LANDED;
    app.spacewalking = false;
    app.astronaut.active = false;
}
// Dev hook: cut a straight shaft `depth` deep and `radius` wide (bite
// radius) into the dominant world, from the surface at an angle (degrees) —
// the same bites the cutter makes, without driving it. A depth under the
// radius leaves a shallow dish. Module._debugDigShaft(angle, depth, radius).
extern "C" EMSCRIPTEN_KEEPALIVE void debugDigShaft(float angle, float depth, float radius) {
    Planet& p = dominantPlanet();
    if (radius < 2.0f) radius = 2.0f;
    Vec2 out(cosd(angle), sind(angle));
    float surf = p.surfaceRadius(angle);
    // Bite centers from the bottom one (its lowest point at `depth`) up to
    // one sitting at the surface.
    float z = depth - radius;
    NearField::Frame nf = nearFrame(0.0f);
    for (int n = 0; n < 400; n++) {
        p.dig(p.center + out * (surf - z), radius);
        if (z < radius) NearField::uproot(p, nf, p.center + out * (surf - z), radius + 4.0f);
        if (z <= 0.0f) break;
        z = z - 6.0f > 0.0f ? z - 6.0f : 0.0f;
    }
    if (app.dominantIdx == app.activeFieldIdx) {
        p.focusCarve(p.center + out * surf);
        carveRefocused(p);
    }
}
// Dev hook: reveal every buried ore clump within `range` of the astronaut —
// for looking at how ore is spread through the ground. Returns how many.
// Module._debugRevealOre(range).
extern "C" EMSCRIPTEN_KEEPALIVE int debugRevealOre(float range) {
    Planet& p = dominantPlanet();
    if (app.dominantIdx != app.activeFieldIdx || !p.carve.ready()) return 0;
    return Deposits::revealAll(p, app.astronaut.pos, range);
}
// Dev hook: go and stand at trading post `idx` of the dominant world — the
// ship parked a little way off, the astronaut on foot `offset` world units
// along the ground from the stall. Returns how many posts the world has
// (idx < 0 just asks). Module._debugVisitOutpost(idx, offset).
extern "C" EMSCRIPTEN_KEEPALIVE int debugVisitOutpost(int idx, float offset) {
    Planet& p = dominantPlanet();
    int n = (int)p.outposts.size();
    if (idx < 0 || idx >= n) return n;
    float perDeg = p.baseRadius * DEG2RAD;
    float a = p.outposts[idx].angle;
    debugParkAt(a - 330.0f / perDeg);
    debugTeleportAngle(a + offset / perDeg);
    return n;
}
// Dev hook: hand the player things to trade with. `what` is an inventory
// item id, 100 for cows in the hold, 200 for credits. Module._debugGive(what, n).
extern "C" EMSCRIPTEN_KEEPALIVE void debugGive(int what, int amount) {
    if (what == 200) addCredits(amount);
    else if (what == Trade::COW) app.cargo = std::min(maxCargo(), app.cargo + amount);
    else addItem(what, amount);
    if (app.tradePost >= 0) pushTrade();
}
// Kind and depth below the ground line of prop `idx` (kind * 10000 + depth),
// or -1: lets a test pick out, say, the deepest crystal.
extern "C" EMSCRIPTEN_KEEPALIVE int debugPropInfo(int idx) {
    const Planet& p = dominantPlanet();
    if (idx < 0 || idx >= (int)p.caveProps.size()) return -1;
    const Planet::CaveProp& q = p.caveProps[idx];
    float under = p.surfaceRadius(atan2d(q.local.y, q.local.x)) - q.local.length();
    return (int)q.kind * 10000 + (int)(under < 0.0f ? 0.0f : under);
}

// Dev aid: snapshot the simulation into window.GAME_DUMP (JSON-able object) for
// tests and console poking. Call Module._debugDump() then read the global.
// Dev: how far straight down from the astronaut's center the first rock is,
// and how far the astronaut is below the terrain's outline at their angle.
static float dbgFloorGap() {
    const Planet& p = dominantPlanet();
    Vec2 up = (app.astronaut.pos - p.center).normalize();
    for (float k = 0.0f; k <= 400.0f; k += 1.0f)
        if (p.rockAt(app.astronaut.pos - up * k) > 0.0f) return k;
    return 400.0f;
}
static float dbgBelowLine() {
    const Planet& p = dominantPlanet();
    Vec2 a = app.astronaut.pos;
    return p.surfaceRadius(atan2d(a.y - p.center.y, a.x - p.center.x)) - dist(a.x, a.y, p.center.x, p.center.y);
}

extern "C" EMSCRIPTEN_KEEPALIVE void debugDump() {
    std::string bodies = "[";
    for (int i = 0; i < (int)app.planets.size(); i++) {
        const Planet& p = app.planets[i];
        char buf[256];
        snprintf(buf, sizeof(buf), "%s{\"i\":%d,\"name\":\"%s\",\"x\":%.1f,\"y\":%.1f,\"vx\":%.3f,\"vy\":%.3f,\"r\":%.0f,\"found\":%d}",
                 i ? "," : "", i, p.name.c_str(), p.center.x, p.center.y, p.orbitVel.x, p.orbitVel.y,
                 p.baseRadius, p.discovered ? 1 : 0);
        bodies += buf;
    }
    bodies += "]";
    char head[1024];
    snprintf(head, sizeof(head),
             "{\"x\":%.1f,\"y\":%.1f,\"vx\":%.3f,\"vy\":%.3f,\"rot\":%.1f,\"state\":%d,\"dominant\":%d,"
             "\"scale\":%.5f,\"walking\":%d,\"ax\":%.1f,\"ay\":%.1f,\"encounter\":%d,\"encDist\":%.0f,"
             "\"cow0x\":%.1f,\"cow0y\":%.1f,\"fx\":%.1f,\"fy\":%.1f,\"avx\":%.3f,\"avy\":%.3f,"
             "\"grounded\":%d,\"mx\":%.1f,\"my\":%.1f,\"cwx\":%.1f,\"cwy\":%.1f,\"lex\":%.1f,\"ley\":%.1f,"
             "\"hover\":%d,\"laser\":%d,\"rotv\":%.2f,\"floorGap\":%.1f,\"belowLine\":%.1f,\"bodies\":",
             app.lander.pos.x, app.lander.pos.y, app.lander.vel.x, app.lander.vel.y, app.lander.rotation,
             (int)app.state, app.dominantIdx, app.view.scale, (int)app.spacewalking,
             app.astronaut.pos.x, app.astronaut.pos.y, app.encounter.body, app.encounter.distance,
             app.cows.empty() ? 0.0f : app.cows[0].pos.x, app.cows.empty() ? 0.0f : app.cows[0].pos.y,
             app.view.focusX, app.view.focusY, app.astronaut.vel.x, app.astronaut.vel.y,
             (int)app.astronaut.grounded, Input::getMouseX(), Input::getMouseY(),
             screenToWorld(Input::getMouseX(), Input::getMouseY()).x,
             screenToWorld(Input::getMouseX(), Input::getMouseY()).y,
             app.laserEnd.x, app.laserEnd.y, app.hoverTarget, (int)app.laserOn, app.view.rotation,
             dbgFloorGap(), dbgBelowLine());
    // Harvestables within laser range of the astronaut, with where they are on
    // screen (canvas pixels) — lets a test aim at them.
    std::string items = ",\"items\":[";
    if (app.spacewalking) {
        const Planet& p = dominantPlanet();
        NearField::Frame f = nearFrame(0.0f);
        Mat3 vm = app.view.matrix(app.width, app.height);
        bool first = true;
        for (int i = 0; i < (int)p.harvest.size(); i++) {
            const Planet::Harvestable& h = p.harvest[i];
            float rel = std::fmod(h.angle - f.viewAngle + 540.0f, 360.0f) - 180.0f;
            if (fabsf(rel) > f.halfSpanDeg) continue;
            Vec2 c = NearField::center(p, h);
            if (dist(c.x, c.y, app.astronaut.pos.x, app.astronaut.pos.y) > LASER_RANGE) continue;
            Vec2 clip = vm.transform(c);
            char buf[160];
            snprintf(buf, sizeof(buf), "%s{\"i\":%d,\"x\":%.0f,\"y\":%.0f,\"kind\":%d,\"depth\":%.0f,\"alive\":%d}",
                     first ? "" : ",", i, (clip.x * 0.5f + 0.5f) * app.width,
                     (1.0f - (clip.y * 0.5f + 0.5f)) * app.height, (int)h.kind, h.depth,
                     NearField::alive(h) ? 1 : 0);
            items += buf;
            first = false;
        }
    }
    items += "]";
    // The same for what grows in the caves, plus how much of each thing is
    // being carried.
    items += ",\"props\":[";
    if (app.spacewalking) {
        const Planet& p = dominantPlanet();
        Mat3 vm = app.view.matrix(app.width, app.height);
        bool first = true;
        for (int i = 0; i < (int)p.caveProps.size(); i++) {
            const Planet::CaveProp& q = p.caveProps[i];
            Vec2 c = CaveField::center(p, q);
            if (dist(c.x, c.y, app.astronaut.pos.x, app.astronaut.pos.y) > LASER_RANGE) continue;
            Vec2 clip = vm.transform(c);
            char buf[160];
            snprintf(buf, sizeof(buf), "%s{\"i\":%d,\"x\":%.0f,\"y\":%.0f,\"kind\":%d,\"alive\":%d,\"hp\":%.2f}",
                     first ? "" : ",", i, (clip.x * 0.5f + 0.5f) * app.width,
                     (1.0f - (clip.y * 0.5f + 0.5f)) * app.height, (int)q.kind,
                     CaveField::alive(q) ? 1 : 0, q.hp);
            items += buf;
            first = false;
        }
    }
    // ...and the ore uncovered near the player.
    items += "],\"ores\":[";
    if (app.spacewalking) {
        const Planet& p = dominantPlanet();
        Mat3 vm = app.view.matrix(app.width, app.height);
        bool first = true;
        for (int i = 0; i < (int)p.ores.size(); i++) {
            const Planet::Ore& o = p.ores[i];
            Vec2 c = Deposits::center(p, o);
            if (dist(c.x, c.y, app.astronaut.pos.x, app.astronaut.pos.y) > LASER_RANGE * 2.0f) continue;
            Vec2 clip = vm.transform(c);
            char buf[160];
            snprintf(buf, sizeof(buf), "%s{\"i\":%d,\"x\":%.0f,\"y\":%.0f,\"kind\":%d,\"mined\":%d,\"hp\":%.2f,\"heat\":%.2f}",
                     first ? "" : ",", i, (clip.x * 0.5f + 0.5f) * app.width,
                     (1.0f - (clip.y * 0.5f + 0.5f)) * app.height, (int)o.kind, o.mined ? 1 : 0, o.hp, o.heat);
            items += buf;
            first = false;
        }
    }
    {
        // Water: is this a wet world, how far the player is above (+) or
        // below (-) its level, and how deep in it they actually are.
        const Planet& p = dominantPlanet();
        Vec2 pp = playerPos();
        char buf[160];
        snprintf(buf, sizeof(buf), "],\"sea\":%d,\"seaAlt\":%.1f,\"wet\":%.1f,\"overWater\":%d",
                 p.sea ? 1 : 0, dist(pp.x, pp.y, p.center.x, p.center.y) - p.seaRadius(),
                 p.waterDepth(pp), p.overWater(atan2d(pp.y - p.center.y, pp.x - p.center.x)) ? 1 : 0);
        items += buf;
    }
    items += ",\"hoverProp\":" + std::to_string(app.hoverProp) + ",\"hoverOre\":" + std::to_string(app.hoverOre) +
             ",\"burnOre\":" + std::to_string(app.burnOre) +
             ",\"tool\":" + std::to_string(app.toolMode) + ",\"inv\":[";
    for (int i = 0; i < App::ITEM_COUNT; i++) items += (i ? "," : "") + std::to_string(app.inventory[i]);
    items += "]";
    // Trading: the money, the upgrade levels, which post is in reach / open,
    // and every post of the world you are at — how far along the ground it
    // is (+ = right) and where its stall is on screen.
    items += ",\"credits\":" + std::to_string(app.credits) + ",\"cargo\":" + std::to_string(app.cargo) +
             ",\"nearPost\":" + std::to_string(app.nearPost) + ",\"tradePost\":" + std::to_string(app.tradePost) +
             ",\"upgrades\":[";
    for (int i = 0; i < Trade::UPGRADES; i++) items += (i ? "," : "") + std::to_string(app.upgrades[i]);
    items += "],\"posts\":[";
    {
        const Planet& p = dominantPlanet();
        Mat3 vm = app.view.matrix(app.width, app.height);
        Vec2 pp = playerPos();
        float pa = atan2d(pp.y - p.center.y, pp.x - p.center.x);
        for (int i = 0; i < (int)p.outposts.size(); i++) {
            const Planet::Outpost& o = p.outposts[i];
            Vec2 clip = vm.transform(Outposts::ground(p, o));
            char buf[200];
            snprintf(buf, sizeof(buf), "%s{\"i\":%d,\"angle\":%.3f,\"along\":%.0f,\"x\":%.0f,\"y\":%.0f,\"keeper\":\"%s\",\"folk\":%d}",
                     i ? "," : "", i, o.angle,
                     (std::fmod(o.angle - pa + 540.0f, 360.0f) - 180.0f) * p.baseRadius * DEG2RAD,
                     (clip.x * 0.5f + 0.5f) * app.width, (1.0f - (clip.y * 0.5f + 0.5f)) * app.height,
                     o.keeper.c_str(), (int)o.folk.size());
            items += buf;
        }
    }
    items += "]";
    std::string json = std::string(head) + bodies + items + "}";
    EM_ASM({ window.GAME_DUMP = JSON.parse(UTF8ToString($0)); }, json.c_str());
}

// Chart a world the first time the player gets close to it.
static void updateDiscovery() {
    Vec2 sp = playerPos();
    for (Planet& p : app.planets) {
        if (p.discovered || p.isSun) continue;
        float reach = p.baseRadius * 2.5f + 6000.0f;
        if (dist(sp.x, sp.y, p.center.x, p.center.y) > reach) continue;
        p.discovered = true;
        app.score += 25;
        showEvent("Discovered " + p.name, p.kind + "  +25");
    }
}

// Push the "where am I" readout to the DOM HUD, only when it changes.
static void updateNavHud() {
    int total = 0, found = 0;
    for (const Planet& p : app.planets) {
        if (p.isSun) continue;
        total++;
        if (p.discovered) found++;
    }
    const Planet& b = dominantPlanet();
    int key = app.dominantIdx * 10000 + found * 10 + (b.discovered ? 1 : 0);
    if (key == app.navHudKey) return;
    app.navHudKey = key;
    EM_ASM({
        if (window.updateNav) window.updateNav(UTF8ToString($0), UTF8ToString($1),
                                               UTF8ToString($2), $3, $4, $5);
    }, b.discovered ? b.name.c_str() : "Uncharted world", b.discovered ? b.kind.c_str() : "",
       app.system.starName.c_str(), found, total, (int)app.system.seed);
}

// The HUD's pointer to the nearest trading post on the world you are at: how
// far along the ground, and which way (right = the way angles grow, with the
// camera rolled upright). Only near the ground, and only when it changes.
static void updateTraderHud() {
    const Planet& p = dominantPlanet();
    bool show = !p.isSun && !p.outposts.empty() && !app.mapMode &&
                (app.spacewalking || app.state != PLAYING || app.lander.altitude < 6000.0f);
    float along = 0.0f;
    int key = -1;
    if (show) {
        int i = Outposts::nearest(p, playerPos(), &along);
        float d = fabsf(along);
        // Steps of ten units close by, a hundred further off.
        int bucket = d < 1000.0f ? (int)(d / 10.0f) : 100 + (int)(d / 100.0f);
        key = ((app.dominantIdx * 8 + i) * 4096 + bucket) * 2 + (along > 0.0f ? 1 : 0);
    }
    if (key == app.traderHudKey) return;
    app.traderHudKey = key;
    EM_ASM({ if (window.setTraderHint) window.setTraderHint($0, $1); }, along, show ? 1 : 0);
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
    // The water-level knob moved: what has flooded was worked out for the old
    // level. Drop the dig window so it is rebuilt (and re-flooded) at the new.
    {
        static float lastWaterLevel = 0.0f;
        if (gDebug.waterLevel != lastWaterLevel) {
            lastWaterLevel = gDebug.waterLevel;
            for (Planet& p : app.planets) p.carve.release();
        }
    }

    // Edge-triggered keys: thrust (lift off from a landing) and R (restart).
    bool thrustKey = Input::isKeyDown(Input::KEY_W) || Input::isKeyDown(Input::KEY_UP);
    bool restartKey = Input::isKeyDown(KEY_R);
    bool thrustPressed = thrustKey && !app.prevThrustKey;
    bool restartPressed = restartKey && !app.prevRestartKey;
    app.prevThrustKey = thrustKey;
    app.prevRestartKey = restartKey;

    // M — toggle the system map (the camera frames every orbit; you keep flying).
    bool mapKey = Input::isKeyDown(KEY_M);
    if (mapKey && !app.prevMapKey) app.mapMode = !app.mapMode;
    app.prevMapKey = mapKey;

    // E — exit ship (when landed) or re-enter (when close enough on foot).
    bool exitKey     = Input::isKeyDown(Input::KEY_E);
    bool exitPressed = exitKey && !app.prevExitKey;
    app.prevExitKey  = exitKey;

    if (exitPressed) {
        if (app.tradePost >= 0) {
            // Done trading.
            closeTrade();
        } else if (app.spacewalking && wouldTrade()) {
            // At a trader's stall: E is "talk to them".
            openTrade(app.nearPost);
        } else if (app.spacewalking) {
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
            float surfR = p.visibleRadius(ang);     // out onto the ground, or into the water it floats on
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
    //
    // Tuning aid: the debug panel can pin the time of day (-5 = auto). The
    // world you're at (or its planet, if you're on a moon) is frozen on its
    // orbit at the angle that puts the star in the requested direction —
    // 270 = noon over the spawn point, ~350 = sunset there.
    int pinIdx = -1;
    if (gDebug.sunOverride >= 0.0f) {
        pinIdx = app.dominantIdx;
        while (app.planets[pinIdx].orbitParent >= 0 &&
               !app.planets[app.planets[pinIdx].orbitParent].isSun)
            pinIdx = app.planets[pinIdx].orbitParent;
    }
    const Vec2 domVelBefore = dominantPlanet().orbitVel;
    for (int i = 0; i < (int)app.planets.size(); i++) {
        Planet& p = app.planets[i];
        p.orbitPinned = (i == pinIdx);
        if (p.orbitPinned) p.orbitAngle = gDebug.sunOverride - 180.0f;
        p.updateOrbit(app.planets, timeScale);
    }
    // Engaging, moving or releasing the pin is not physics: the world you're
    // at jumps along its orbit and its velocity changes in a single frame.
    // Carry a flying ship (and the camera) through the same jump so the slider
    // can be used mid-flight; landed ships, the astronaut and the cows already
    // ride the body's orbitStep.
    bool pinEvent = pinIdx != app.prevPinIdx ||
                    (pinIdx >= 0 && gDebug.sunOverride != app.prevSunOverride);
    app.prevPinIdx = pinIdx;
    app.prevSunOverride = gDebug.sunOverride;
    if (pinEvent) {
        const Planet& dom = dominantPlanet();
        // The part of this frame's move that was a teleport rather than motion.
        Vec2 jump = dom.orbitStep - dom.orbitVel * timeScale;
        app.view.focusX += jump.x;
        app.view.focusY += jump.y;
        if (app.state == PLAYING) {
            app.lander.pos += jump;
            app.lander.vel += dom.orbitVel - domVelBefore;
        }
    }
    if (app.state == PLAYING && !app.spacewalking) {
        app.dominantIdx = dominantBody(app.lander.pos);
    }
    updateActiveField();

    // Everything standing on a world rides along with it. The cows live on the
    // home planet; carry them by its exact move this frame so their own
    // (ground-relative) motion is all that's left to simulate.
    for (Cow& cow : app.cows) {
        if (cow.state != Cow::State::Stowed) cow.pos += homePlanet().orbitStep;
    }

    switch (app.state) {
        case PLAYING:
            pollInput(timeScale);
            app.lander.update(app.planets, timeScale);
            updateContact();
            // Flying into the star ends the run.
            if (app.lander.active) {
                const Planet& sun = app.planets[app.sunIdx];
                if (dist(app.lander.pos.x, app.lander.pos.y, sun.center.x, sun.center.y) <
                    sun.baseRadius * 0.9f) {
                    app.lander.crash();
                    app.stuckIdx = app.sunIdx;
                    app.lander.landingOffset = app.lander.pos - sun.center;
                    app.state = CRASHED;
                }
            }
            app.lander.updateBeam(dominantPlanet());
            runBeamGrab();
            updateCows(timeScale);
            break;
        case LANDED: {
            // Stick to the body so the ship rides its orbit while parked, and
            // share its velocity (the camera's follow prediction and the HUD
            // speed both read it).
            const Planet& body = app.planets[app.stuckIdx];
            app.lander.settle(body, timeScale);
            app.lander.pos = body.center + app.lander.landingOffset;
            app.lander.vel = body.orbitVel;
            updateContact();                 // keep altitude readout fresh
            // A safe landing is a pit stop: the tank tops itself back up.
            if (gDebug.refuelRate > 0.0f && app.lander.fuel < maxFuel()) {
                app.lander.fuel += gDebug.refuelRate * timeScale;
                if (app.lander.fuel > maxFuel()) app.lander.fuel = maxFuel();
            }
            if (!app.spacewalking && thrustPressed) {
                app.lander.liftOff(body);
                app.state = PLAYING;
            }
            // The herd keeps living while you are parked or out on foot: that
            // is when the ground can be cut out from under one.
            updateCows(timeScale);
            break;
        }
        case CRASHED: {
            // The wreck stays where it fell while the world moves on.
            const Planet& body = app.planets[app.stuckIdx];
            app.lander.pos = body.center + app.lander.landingOffset;
            app.lander.vel = body.orbitVel;
            if (restartPressed) resetGame();
            break;
        }
    }

    if (app.spacewalking) {
        // The astronaut simulates in the body's co-moving frame: carry them
        // with its orbital motion first, so their velocity stays relative to
        // the ground. Without this the planet slides out from under them at
        // orbital speed — far faster than they can walk — and ground friction
        // (which damps toward zero world velocity) makes catching up
        // impossible. orbitStep is the exact move updateOrbit just applied.
        app.astronaut.pos += dominantPlanet().orbitStep;
        // Keep the fine dig layer around the player BEFORE moving them: their
        // collision reads it.
        if (app.dominantIdx == app.activeFieldIdx && dominantPlanet().focusCarve(app.astronaut.pos))
            carveRefocused(dominantPlanet());
        pollInputAstronaut(timeScale);
        app.astronaut.update(dominantPlanet(), timeScale);

        // Q switches the hand tool between mining laser and terrain cutter.
        bool toolKey = Input::isKeyDown(Input::KEY_Q);
        if (toolKey && !app.prevToolKey && app.cutterUnlocked) {
            app.toolMode ^= 1;
            EM_ASM({ if (window.setTool) window.setTool($0); }, app.toolMode);
        }
        app.prevToolKey = toolKey;
    } else if ((app.state == LANDED || (app.state == PLAYING && app.lander.altitude < 1500.0f)) &&
               app.dominantIdx == app.activeFieldIdx && dominantPlanet().hasDigs()) {
        // Parked, or flying low: show the caves and whatever has been dug
        // around the ship (the fine layer only covers ~3000 units around it).
        if (dominantPlanet().focusCarve(app.lander.pos)) carveRefocused(dominantPlanet());
    }

    // The locals go about their day, and look up when you come by. Is a
    // trader in reach? (Walking off, or boarding the ship, ends a trade.)
    {
        Planet& dp = dominantPlanet();
        Outposts::update(dp, dt, playerPos(), app.spacewalking);
        app.nearPost = app.spacewalking ? Outposts::inReach(dp, app.astronaut.pos) : -1;
        if (app.tradePost >= 0 && app.nearPost != app.tradePost) closeTrade();
        // The page's hint for what E does here.
        std::string prompt;
        if (app.tradePost < 0 && wouldTrade()) prompt = "Trade with " + dp.outposts[app.nearPost].keeper;
        if (prompt != app.promptShown) {
            app.promptShown = prompt;
            EM_ASM({ if (window.setPrompt) window.setPrompt(UTF8ToString($0)); }, prompt.c_str());
        }
    }

    updateDiscovery();
    updateZoom(timeScale);
    updateView(timeScale);
    updateShootingStar(dt);

    // Mining laser, regrowth of what it cut, and the bits it throws off. After
    // the camera: the cursor is turned into a world point through the view,
    // and the world moves ~60 units a frame along its orbit — aiming through
    // last frame's view would put the beam that far off target.
    updateLaser(dt, (float)(now - app.startTime) * 3.0f);
    NearField::update(dominantPlanet(), dt, app.laserTarget);
    CaveField::update(dominantPlanet(), dt, app.laserProp);
    Deposits::update(dominantPlanet(), dt, app.burnOre);
    updateWaterFx(dt, (float)(now - app.startTime) * 3.0f);
    updateParticles(dt, timeScale);
    for (App::Burn& b : app.burns) {
        if (b.heat > 0.0f) b.heat -= dt / 1.7f;       // cools in under two seconds
    }
    // Down a hole? (On the surface the astronaut's center is a couple of units
    // ABOVE the terrain line.) Eases, so the front ground thins and firms up
    // rather than popping.
    {
        float want = 0.0f;
        if (app.spacewalking) {
            const Planet& hp = dominantPlanet();
            Vec2 ap = app.astronaut.pos;
            float under = hp.surfaceRadius(atan2d(ap.y - hp.center.y, ap.x - hp.center.x)) -
                          dist(ap.x, ap.y, hp.center.x, hp.center.y);
            want = under > 6.0f ? 1.0f : 0.0f;
        }
        float k = 0.14f * timeScale;
        app.inHole += (want - app.inHole) * (k > 1.0f ? 1.0f : k);
    }

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

    glStencilMask(0xFF);
    glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    Mat3 viewMat = app.view.matrix(app.width, app.height);

    // 1) Deep background: starfield.
    app.renderer.begin(viewMat, app.width, app.height);
    drawStars();
    app.renderer.end();

    // 1a) The sun: additive corona + core at its true world position, behind
    //     the planets so they occlude it when they pass in front.
    drawSun(viewMat);

    // 1b) Star occluders: black out the starfield behind the (translucent)
    //     mountain layers and behind the other worlds' discs, before the sky
    //     is laid over it.
    drawRidges(viewMat, true);
    drawSkyBodies(viewMat, true);

    // 1c) Sky band (atmosphere mode 0): the open sky above the terrain line.
    drawAtmospherePass(shaderTime, 0);

    // 1d) Distant ridge silhouettes, painted over the sky. Full unclipped
    //     bands — the terrain body drawn later occludes everything below the
    //     horizon, which is what makes them read as ranges BEYOND it. They
    //     also stamp the stencil, so the next group stays behind them.
    drawRidges(viewMat);

    // 1d1) Everything that lives BEHIND the mountains, clipped to where no
    //      ridge is: the system's other worlds and the star on the sky dome
    //      (they rise and set behind the ranges), the far hazy cloud band,
    //      and the occasional shooting star.
    beginBehindRidges();
    drawSkyBodies(viewMat);
    drawFarClouds(viewMat, shaderTime);
    drawShootingStar();
    endBehindRidges();

    // 1e) Volumetric light shafts raking through the sky; the body (next) clips
    //     their lower ends so they read as landing on the ground.
    drawLightShafts(viewMat, shaderTime);

    // 2) Planet bodies: SDF fill + material + slope lighting + crosshatch +
    //    day/night terminator, one fullscreen pass over every body (near
    //    planet from the live density field, far ones from silhouette rows).
    //    The water goes down first, behind the crust: it shows wherever the
    //    ground is lower than the sea.
    drawWaterPass(0, shaderTime);
    drawBodyPass();
    // ...and mark the cave's open space in the stencil, for the formations
    // that grow out of its walls (beginInCave).
    drawCaveMask();

    // 2a) Long cast shadows from peaks, darkening the valleys behind them.
    drawTerrainShadows(viewMat);

    // 2b) Ground shadows + cows, on top of the shaded body.
    app.renderer.begin(viewMat, app.width, app.height);
    drawEntityShadows();
    for (const Cow& cow : app.cows) cow.draw(app.renderer, homePlanet());
    app.renderer.end();

    // 2c) Harvestable plants and minerals standing BEHIND the walking line
    //     (between it and the horizon edge), over the terrain.
    {
        NearField::Frame nf = nearFrame(shaderTime);
        app.renderer.begin(viewMat, app.width, app.height);
        NearField::draw(app.renderer, dominantPlanet(), nf, false, app.laserTarget);
        // The locals' trading posts stand on that strip too: hut, stall,
        // trader, the others out walking. The ship and the astronaut are
        // drawn later, so they pass in front.
        Outposts::draw(app.renderer, dominantPlanet(), nf, app.view.scale,
                       app.tradePost >= 0 ? app.tradePost : (wouldTrade() ? app.nearPost : -1));
        // ...the ore uncovered in the rock, and what grows in the caves,
        // behind the walker. (Underground, these only show to someone who is
        // down there; the front-ground veil and their own fade see to that.)
        // The formations are clipped to the cave's open space, so the wall
        // hides their roots.
        CaveField::Frame cf = caveFrame(shaderTime);
        Deposits::draw(app.renderer, dominantPlanet(), cf, app.laserOre, app.burnOre);
        CaveField::draw(app.renderer, dominantPlanet(), cf, false, app.laserProp, CaveField::Part::Loose);
        app.renderer.end();
        beginInCave();
        app.renderer.begin(viewMat, app.width, app.height);
        CaveField::draw(app.renderer, dominantPlanet(), cf, false, app.laserProp, CaveField::Part::Walled);
        app.renderer.end();
        endInCave();
    }

    // 3) Face fill (atmosphere mode 1): from space, the sky sphere tints the
    //    planet face; near the surface it's gated to zero so the ground stays
    //    clear.
    drawAtmospherePass(shaderTime, 1);

    // 3b) Navigation overlay (orbit rings, world markers, name labels) once
    //     the view is far enough out that worlds are specks.
    drawNavOverlay(viewMat);

    // 3c) Ship + astronaut + space aids stay crisp on top of the sky.
    app.renderer.begin(viewMat, app.width, app.height);
    if (spaceAids) drawTrajectory();
    app.lander.drawBeam(app.renderer);
    app.lander.draw(app.renderer);   // always draw the actual ship
    // The astronaut after the ship: stepping out (and walking past it) they
    // are in front of it, never hidden behind the hull.
    if (app.spacewalking) app.astronaut.draw(app.renderer, dominantPlanet());
    // Cave formations standing in front of the walking plane (the walled ones
    // clipped to the cave's open space, as behind).
    {
        CaveField::Frame cf = caveFrame(shaderTime);
        CaveField::draw(app.renderer, dominantPlanet(), cf, true, app.laserProp, CaveField::Part::Loose);
        app.renderer.end();
        beginInCave();
        app.renderer.begin(viewMat, app.width, app.height);
        CaveField::draw(app.renderer, dominantPlanet(), cf, true, app.laserProp, CaveField::Part::Walled);
        app.renderer.end();
        endInCave();
        app.renderer.begin(viewMat, app.width, app.height);
    }
    if (spaceAids || app.navAlpha > 0.5f) {
        // In space the ship is sub-pixel, so overlay a locator beacon on top of
        // it (also on the map while parked: it's the "you are here").
        drawShipBeacon();
        if (spaceAids && steering) drawHeadingArrow();
    }
    app.renderer.end();

    // 3c-) The water in front of the walking line: over the ship and the
    //      astronaut where they are in it, and standing in flooded holes.
    //      Before the veil, which is ground in front of that water.
    drawWaterPass(1, shaderTime);

    // 3c0) Front-ground veil: the strip of ground in front of the walking line,
    //      drawn back over any hole that passes behind it — and over the
    //      astronaut while they are down there — see-through. After the
    //      player, before the plants that stand on that strip.
    drawBodyPass(BodyPass::Veil);
    //      ...and the water standing over that strip, where it is lake bed.
    drawWaterPass(2, shaderTime);

    // 3c1) Harvestables IN FRONT of the walking line, over the ship and the
    //      astronaut — you walk behind the grass — then the mining laser, its
    //      sparks and the harvested bits on top of those.
    {
        NearField::Frame nf = nearFrame(shaderTime);
        app.renderer.begin(viewMat, app.width, app.height);
        NearField::draw(app.renderer, dominantPlanet(), nf, true, app.laserTarget);
        drawLaserFx(shaderTime);
        app.renderer.end();
    }

    // 3d) Near framing planes: dark silhouette bands (plus their spires and
    //     fronds) along the bottom of the frame, drawn after the ship because
    //     they are the closest depth plane. Screen-anchored, so they frame the
    //     scene without ever covering the player.
    drawForeground(shaderTime);

    // 4) Clouds (alpha) in front of everything, incl. the ship (fly behind them).
    drawCloudsPass(shaderTime);

    // 5) Drifting dust motes (atmospheric volume), then the framing vignette —
    //    the topmost screen-space layers.
    drawMotes(shaderTime);
    drawVignette();

    // Push HUD state to the DOM overlay (defined in shell.html).
    EM_ASM({
        if (window.updateHUD) window.updateHUD($0, $1, $2, $3, $4, $5, $6, $7, $8);
    }, app.lander.fuel, maxFuel(), app.lander.altitude,
       // Speed relative to the world you're at — its own orbital motion is
       // tens of units a frame and would swamp the number you fly by.
       (app.lander.vel - dominantPlanet().orbitVel).length(),
       (int)app.state, app.cargo, maxCargo(), app.score,
       (int)app.spacewalking);
    updateNavHud();
    updateTraderHud();
    if (app.creditsDirty) {
        app.creditsDirty = false;
        EM_ASM({ if (window.setCredits) window.setCredits($0); }, app.credits);
    }
    if (app.inventoryDirty) {
        app.inventoryDirty = false;
        std::string counts;
        for (int i = 0; i < App::ITEM_COUNT; i++) counts += std::to_string(app.inventory[i]) + ",";
        EM_ASM({ if (window.setInventory) window.setInventory(UTF8ToString($0)); }, counts.c_str());
    }
}

int main() {
    EmscriptenWebGLContextAttributes attrs;
    emscripten_webgl_init_context_attributes(&attrs);
    attrs.majorVersion = 2;
    attrs.minorVersion = 0;
    attrs.alpha = false;
    attrs.depth = false;
    attrs.stencil = true;    // "behind the mountains" clipping (see drawRidges)
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
