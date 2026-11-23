#include "system_gen.h"
#include <cmath>

namespace {

struct Rng {
    unsigned int state;
    explicit Rng(unsigned int s) : state(s ? s : 1) {
        // Scramble the seed so neighboring seeds (1, 2, 3...) don't produce
        // near-identical first draws from the LCG.
        for (int i = 0; i < 4; i++) next01();
    }
    float next01() {
        state = state * 1664525u + 1013904223u;
        return (state >> 8) * (1.0f / 16777216.0f);
    }
    float range(float a, float b) { return a + next01() * (b - a); }
    int irange(int a, int b) {   // inclusive
        int v = a + (int)std::floor(next01() * (float)(b - a + 1));
        return v > b ? b : v;
    }
    bool chance(float p) { return next01() < p; }
    unsigned int nextSeed() {
        state = state * 1664525u + 1013904223u;
        return (state >> 4) | 1u;
    }
};

void set3(float dst[3], float r, float g, float b) { dst[0] = r; dst[1] = g; dst[2] = b; }
void hex3(float dst[3], unsigned int rgb) {
    set3(dst, ((rgb >> 16) & 255) / 255.0f, ((rgb >> 8) & 255) / 255.0f, (rgb & 255) / 255.0f);
}
void mix3(float dst[3], const float a[3], const float b[3], float t) {
    for (int i = 0; i < 3; i++) dst[i] = a[i] + (b[i] - a[i]) * t;
}

void hsv3(float dst[3], float h, float s, float v) {
    h = std::fmod(h, 360.0f);
    if (h < 0) h += 360.0f;
    float c = v * s;
    float hp = h / 60.0f;
    float x = c * (1.0f - std::fabs(std::fmod(hp, 2.0f) - 1.0f));
    float r = 0, g = 0, b = 0;
    if (hp < 1)      { r = c; g = x; }
    else if (hp < 2) { r = x; g = c; }
    else if (hp < 3) { g = c; b = x; }
    else if (hp < 4) { g = x; b = c; }
    else if (hp < 5) { r = x; b = c; }
    else             { r = c; b = x; }
    float m = v - c;
    set3(dst, r + m, g + m, b + m);
}

void toHsv(const float c[3], float& h, float& s, float& v) {
    float mx = std::fmax(c[0], std::fmax(c[1], c[2]));
    float mn = std::fmin(c[0], std::fmin(c[1], c[2]));
    float d = mx - mn;
    v = mx;
    s = mx > 1e-6f ? d / mx : 0.0f;
    if (d < 1e-6f) { h = 0; return; }
    if (mx == c[0])      h = 60.0f * std::fmod((c[1] - c[2]) / d, 6.0f);
    else if (mx == c[1]) h = 60.0f * ((c[2] - c[0]) / d + 2.0f);
    else                 h = 60.0f * ((c[0] - c[1]) / d + 4.0f);
    if (h < 0) h += 360.0f;
}

// Nudge a color's hue (degrees) and value (multiplier) — per-seed variation
// on a hand-picked archetype palette, so two desert worlds aren't twins.
void jitter3(float c[3], float dHue, float vMul) {
    float h, s, v;
    toHsv(c, h, s, v);
    v *= vMul;
    if (v > 1.0f) v = 1.0f;
    hsv3(c, h + dHue, s, v);
}

enum class Arch { Temperate, Desert, Ice, Volcanic, Jungle, Exotic, Barren };

// Everything about a body that follows from its archetype: look, air, terrain
// shape. Temperate keeps PlanetLook's defaults — the original home-world look.
void applyArchetype(Planet& p, Arch a, Rng& rng, bool isMoon, TerrainStyle& style) {
    PlanetLook& L = p.look;
    p.atmosphere = true;
    p.archetype = (int)a;
    float dHue = rng.range(-12.0f, 12.0f);
    float vMul = rng.range(0.92f, 1.08f);
    bool derive = true;   // ridges / foreground / signature computed from the palette

    switch (a) {
    case Arch::Temperate:
        p.kind = "Temperate world";
        derive = false;   // hand-tuned defaults
        style.seaChance = 1.0f; style.seaMin = 0.17f; style.seaMax = 0.23f;   // lakes and a sea or two
        break;

    case Arch::Desert:
        p.kind = "Desert world";
        hex3(L.tones[0], 0x5a3626); hex3(L.tones[1], 0x8a5236); hex3(L.tones[2], 0xb87a4a);
        hex3(L.tones[3], 0xd9a868); hex3(L.tones[4], 0xf0d9a8);
        set3(L.skyBeta, 0.0210f, 0.0135f, 0.0085f);   // butterscotch day, cool dusk
        set3(L.mieBeta, 0.0100f, 0.0080f, 0.0060f);
        L.skyGain = 1.0f; L.atmoScale = 0.6f; L.dragScale = 0.55f;
        set3(L.haze, 0.86f, 0.70f, 0.58f);
        L.cloudCover = 0.3f;
        set3(L.cloudLit, 0.98f, 0.88f, 0.76f); set3(L.cloudDark, 0.55f, 0.42f, 0.38f);
        L.frondFrac = 0.1f; L.propDensity = 0.4f;
        set3(L.flora, 0.62f, 0.58f, 0.30f); set3(L.mineral, 0.98f, 0.62f, 0.30f);   // dry scrub, amber
        L.floraDensity = 0.35f; L.mineralDensity = 0.60f;
        style.rangesMin = 0; style.rangesMax = 1; style.rangeBoost = 0.8f; style.rolling = 0.8f;
        // Mostly bone dry; now and then a few turquoise waterholes.
        style.seaChance = 0.4f; style.seaMin = 0.03f; style.seaMax = 0.07f;
        set3(L.water, 0.30f, 0.60f, 0.56f); set3(L.waterDeep, 0.12f, 0.36f, 0.40f);
        break;

    case Arch::Ice:
        p.kind = "Ice world";
        hex3(L.tones[0], 0x2f4358); hex3(L.tones[1], 0x4f6f8c); hex3(L.tones[2], 0x86a6c0);
        hex3(L.tones[3], 0xbcd6e6); hex3(L.tones[4], 0xf4fbff);
        set3(L.skyBeta, 0.0150f, 0.0230f, 0.0290f);   // pale, milky cyan
        set3(L.mieBeta, 0.0120f, 0.0120f, 0.0120f);
        L.skyGain = 0.9f; L.atmoScale = 0.8f; L.dragScale = 0.8f;
        set3(L.haze, 0.80f, 0.88f, 0.94f);
        L.cloudCover = 1.1f;
        set3(L.cloudLit, 0.96f, 0.99f, 1.0f); set3(L.cloudDark, 0.50f, 0.58f, 0.68f);
        L.frondFrac = 0.0f; L.propDensity = 0.5f;     // crystal spires, no fronds
        set3(L.flora, 0.55f, 0.75f, 0.80f); set3(L.mineral, 0.78f, 0.93f, 1.0f);    // frost lichen, ice
        L.floraDensity = 0.20f; L.mineralDensity = 0.90f;
        style.rangesMin = 1; style.rangesMax = 2; style.rangeBoost = 0.9f; style.rolling = 1.3f;
        // Cold meltwater in the hollows.
        style.seaChance = 0.85f; style.seaMin = 0.08f; style.seaMax = 0.18f;
        set3(L.water, 0.50f, 0.69f, 0.77f); set3(L.waterDeep, 0.21f, 0.38f, 0.53f);
        break;

    case Arch::Volcanic:
        p.kind = "Volcanic world";
        hex3(L.tones[0], 0x1c1a1d); hex3(L.tones[1], 0x302a2c); hex3(L.tones[2], 0x4a3d3a);
        hex3(L.tones[3], 0x6b5850); hex3(L.tones[4], 0xd9643a);   // ember-capped peaks
        set3(L.skyBeta, 0.0170f, 0.0080f, 0.0050f);   // smoggy red-brown
        set3(L.mieBeta, 0.0100f, 0.0070f, 0.0050f);
        L.skyGain = 0.8f; L.atmoScale = 1.1f; L.dragScale = 1.3f;
        set3(L.haze, 0.42f, 0.30f, 0.27f);
        L.cloudCover = 1.0f;
        set3(L.cloudLit, 0.50f, 0.42f, 0.40f); set3(L.cloudDark, 0.20f, 0.16f, 0.16f);
        L.frondFrac = 0.0f; L.propDensity = 0.35f;
        set3(L.flora, 0.40f, 0.30f, 0.28f); set3(L.mineral, 1.0f, 0.46f, 0.20f);    // char scrub, ember
        L.floraDensity = 0.15f; L.mineralDensity = 0.80f;
        style.rangesMin = 2; style.rangesMax = 3; style.rangeBoost = 1.25f; style.rolling = 1.0f;
        break;

    case Arch::Jungle:
        p.kind = "Jungle world";
        hex3(L.tones[0], 0x1f3a36); hex3(L.tones[1], 0x2f5a44); hex3(L.tones[2], 0x5a8050);
        hex3(L.tones[3], 0xa09a70); hex3(L.tones[4], 0xeef2e4);
        set3(L.skyBeta, 0.0060f, 0.0200f, 0.0220f);   // humid teal
        set3(L.mieBeta, 0.0090f, 0.0090f, 0.0090f);
        L.skyGain = 0.9f; L.atmoScale = 1.15f; L.dragScale = 1.2f;
        set3(L.haze, 0.60f, 0.78f, 0.76f);
        L.cloudCover = 1.2f;
        set3(L.cloudLit, 0.95f, 1.0f, 0.95f); set3(L.cloudDark, 0.36f, 0.50f, 0.50f);
        L.frondFrac = 0.9f; L.propDensity = 1.6f;
        set3(L.flora, 0.25f, 0.62f, 0.40f); set3(L.mineral, 0.85f, 0.95f, 0.50f);   // lush green, lime
        L.floraDensity = 1.8f; L.mineralDensity = 0.30f;
        style.rangesMin = 1; style.rangesMax = 2; style.rangeBoost = 1.0f; style.rolling = 1.5f;
        // The wettest kind of world: green water, and a lot of it.
        style.seaChance = 1.0f; style.seaMin = 0.24f; style.seaMax = 0.36f;
        set3(L.water, 0.24f, 0.52f, 0.47f); set3(L.waterDeep, 0.09f, 0.30f, 0.33f);
        break;

    case Arch::Exotic: {
        // Fully hue-driven: rock in one hue family, sky from across the wheel.
        p.kind = "Exotic world";
        float h = rng.range(0.0f, 360.0f);
        hsv3(L.tones[0], h, 0.45f, 0.24f);
        hsv3(L.tones[1], h, 0.42f, 0.40f);
        hsv3(L.tones[2], h + 12.0f, 0.36f, 0.58f);
        hsv3(L.tones[3], h + 25.0f, 0.26f, 0.76f);
        hsv3(L.tones[4], h + 180.0f, 0.22f, 0.95f);
        float hs = h + rng.range(120.0f, 240.0f);
        float sky[3];
        hsv3(sky, hs, 0.72f, 1.0f);
        set3(L.skyBeta, sky[0] * 0.030f + 0.002f, sky[1] * 0.030f + 0.002f, sky[2] * 0.030f + 0.002f);
        set3(L.mieBeta, 0.0080f, 0.0080f, 0.0080f);
        L.skyGain = 0.9f;
        L.atmoScale = rng.range(0.7f, 1.2f);
        L.dragScale = rng.range(0.7f, 1.2f);
        hsv3(L.haze, hs, 0.22f, 0.86f);
        L.cloudCover = rng.range(0.3f, 1.0f);
        hsv3(L.cloudLit, hs + 30.0f, 0.10f, 1.0f);
        hsv3(L.cloudDark, hs, 0.25f, 0.50f);
        L.frondFrac = rng.next01(); L.propDensity = rng.range(0.5f, 1.4f);
        hsv3(L.flora, h + 90.0f, 0.50f, 0.62f);
        hsv3(L.mineral, h + 200.0f, 0.50f, 0.96f);
        L.floraDensity = rng.range(0.2f, 1.5f); L.mineralDensity = rng.range(0.3f, 0.9f);
        style.rangesMin = 0; style.rangesMax = 3;
        style.rangeBoost = rng.range(0.8f, 1.3f); style.rolling = rng.range(0.8f, 1.6f);
        // Some liquid or other, in a hue off the rock's — as likely as not.
        style.seaChance = 0.65f; style.seaMin = 0.08f; style.seaMax = 0.30f;
        hsv3(L.water, h + 150.0f, 0.46f, 0.66f);
        hsv3(L.waterDeep, h + 165.0f, 0.60f, 0.40f);
        dHue = 0.0f; vMul = 1.0f;   // already seeded by hue
        break;
    }

    case Arch::Barren: {
        // Airless rock: a grey ramp with a faint mineral tint, pocked with craters.
        p.kind = isMoon ? "Barren moon" : "Barren world";
        p.atmosphere = false;
        float h = rng.range(0.0f, 360.0f);
        float s = rng.range(0.03f, 0.14f);
        hsv3(L.tones[0], h, s, 0.20f);
        hsv3(L.tones[1], h, s, 0.34f);
        hsv3(L.tones[2], h, s * 0.9f, 0.48f);
        hsv3(L.tones[3], h, s * 0.8f, 0.62f);
        hsv3(L.tones[4], h, s * 0.5f, 0.80f);
        L.cloudCover = 0.0f; L.propDensity = 0.0f; L.frondFrac = 0.0f;
        hsv3(L.mineral, h + 150.0f, 0.38f, 0.92f);    // nothing grows; ore and crystal only
        L.floraDensity = 0.0f; L.mineralDensity = 0.70f;
        style.rangesMin = 0; style.rangesMax = 1; style.rangeBoost = 0.8f;
        style.craters = rng.irange(2, 5);
        dHue = 0.0f; vMul = 1.0f;
        break;
    }
    }

    if (!derive) return;

    if (dHue != 0.0f || vMul != 1.0f) {
        for (int k = 0; k < 5; k++) jitter3(L.tones[k], dHue, vMul);
        jitter3(L.haze, dHue * 0.5f, 1.0f);
    }

    // Ridge tints: the nearest range sits between the two darkest terrain
    // tones; the farthest has mostly dissolved into the haze.
    mix3(L.ridgeNear, L.tones[1], L.tones[0], 0.5f);
    mix3(L.ridgeFar, L.ridgeNear, L.haze, 0.55f);
    // Near framing plane: the basin tone pulled most of the way to blue-black.
    const float inkBlue[3] = {0.10f, 0.11f, 0.16f};
    float dark[3] = {L.tones[0][0] * 0.55f, L.tones[0][1] * 0.55f, L.tones[0][2] * 0.55f};
    mix3(L.fgTint, dark, inkBlue, 0.45f);

    // Signature color: the day sky for worlds with air, the mid rock otherwise.
    if (p.atmosphere) {
        float mx = std::fmax(L.skyBeta[0], std::fmax(L.skyBeta[1], L.skyBeta[2]));
        float g = L.skyGain < 1.0f ? L.skyGain : 1.0f;
        for (int i = 0; i < 3; i++) L.sig[i] = (L.skyBeta[i] / mx * 0.85f + 0.10f) * g;
    } else {
        for (int i = 0; i < 3; i++) L.sig[i] = L.tones[2][i];
    }
}

// Pronounceable proper names from syllables. `used` keeps a system free of
// duplicates.
std::string makeName(Rng& rng, std::vector<std::string>& used) {
    static const char* first[] = {"Ka", "Ve", "Zo", "Mi", "Tal", "Or", "Ny", "Eb", "Us", "Phi",
                                  "Dra", "Lu", "Xe", "Ar", "Cor", "Sel", "Tha", "Io", "Bre", "Qua",
                                  "Hel", "Um", "Ry", "Os", "Vel", "Ja", "Ith", "Eno", "Sy", "Mar"};
    static const char* mid[] = {"ri", "lo", "na", "ve", "mu", "ta", "ki", "sa", "ro", "the",
                                "li", "go", "da", "ne", "vi", "ra"};
    static const char* last[] = {"s", "n", "x", "ra", "on", "is", "us", "ar", "a", "e",
                                 "os", "um", "ia", "el", "th", "or"};
    const int nf = sizeof(first) / sizeof(first[0]);
    const int nm = sizeof(mid) / sizeof(mid[0]);
    const int nl = sizeof(last) / sizeof(last[0]);
    for (int attempt = 0; attempt < 40; attempt++) {
        std::string n = first[rng.irange(0, nf - 1)];
        if (rng.chance(0.55f)) n += mid[rng.irange(0, nm - 1)];
        n += last[rng.irange(0, nl - 1)];
        bool dup = false;
        for (const std::string& u : used) if (u == n) { dup = true; break; }
        if (!dup) { used.push_back(n); return n; }
    }
    std::string fallback = "Body " + std::to_string(used.size() + 1);
    used.push_back(fallback);
    return fallback;
}

struct BodySpec {
    Arch arch = Arch::Barren;
    float radius = 10000.0f;
    float noise = 600.0f;
    int points = 160;
    float gSurface = 0.05f;      // surface gravity, world units / frame^2
    bool hasMoon = false;
    float moonRadius = 0.0f;
    float moonOrbit = 0.0f;
    float reach = 0.0f;          // how far its neighborhood extends (moon orbit / sky top)
};

int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

}  // namespace

void updateKeplerSpeeds(std::vector<Planet>& planets) {
    for (Planet& p : planets) {
        if (p.orbitParent < 0 || p.orbitParent >= (int)planets.size()) continue;
        p.setKeplerSpeed(planets[p.orbitParent].gravity);
    }
}

StarSystem generateSystem(unsigned int seed, std::vector<Planet>& planets) {
    Rng rng(seed * 2654435761u + 0x9e3779b9u);
    std::vector<std::string> names;
    StarSystem sys;
    sys.seed = seed;
    sys.starName = makeName(rng, names);

    const float G0 = Planet::REFERENCE_DENSITY;   // generation happens at the reference knob

    // ---- Decide the lineup -------------------------------------------------
    int planetCount = rng.irange(4, 6);
    bool hasInner = rng.chance(0.6f);
    int homeSlot = hasInner ? 1 : 0;

    std::vector<BodySpec> specs(planetCount);
    // Variety: a system shouldn't hand out the same kind of world twice while
    // there are kinds it hasn't used (plain rock is the exception — there can
    // be several barren worlds).
    std::vector<Arch> usedKinds;
    usedKinds.push_back(Arch::Temperate);   // the home world's
    for (int i = 0; i < planetCount; i++) {
        BodySpec& b = specs[i];
        if (i == homeSlot) {
            // The home world: original tuning, always with its moon.
            b.arch = Arch::Temperate;
            b.radius = 32000.0f; b.noise = 2000.0f; b.points = 255; b.gSurface = 0.1f;
            b.hasMoon = true;
            b.moonRadius = rng.range(5000.0f, 7000.0f);
            b.moonOrbit = b.radius * 3.5f;
        } else {
            // Archetype by distance from the star: scorched inside the home
            // orbit, anything goes just outside it, frozen at the edge.
            float t = planetCount > 1 ? (float)i / (planetCount - 1) : 0.0f;
            const Arch inner[] = {Arch::Desert, Arch::Volcanic, Arch::Barren};
            const Arch middle[] = {Arch::Desert, Arch::Jungle, Arch::Exotic, Arch::Barren, Arch::Volcanic};
            const Arch outer[] = {Arch::Ice, Arch::Exotic, Arch::Barren, Arch::Ice};
            for (int attempt = 0; attempt < 10; attempt++) {
                if (i < homeSlot)   b.arch = inner[rng.irange(0, 2)];
                else if (t < 0.7f)  b.arch = middle[rng.irange(0, 4)];
                else                b.arch = outer[rng.irange(0, 3)];
                bool repeat = false;
                for (Arch u : usedKinds) if (u == b.arch && b.arch != Arch::Barren) repeat = true;
                if (!repeat) break;
            }
            usedKinds.push_back(b.arch);
            switch (b.arch) {
                case Arch::Desert:   b.radius = rng.range(12000.0f, 22000.0f); b.noise = b.radius * rng.range(0.050f, 0.075f); break;
                case Arch::Ice:      b.radius = rng.range(14000.0f, 26000.0f); b.noise = b.radius * rng.range(0.045f, 0.070f); break;
                case Arch::Volcanic: b.radius = rng.range(12000.0f, 20000.0f); b.noise = b.radius * rng.range(0.070f, 0.100f); break;
                case Arch::Jungle:   b.radius = rng.range(20000.0f, 30000.0f); b.noise = b.radius * rng.range(0.050f, 0.080f); break;
                case Arch::Exotic:   b.radius = rng.range(12000.0f, 28000.0f); b.noise = b.radius * rng.range(0.050f, 0.090f); break;
                default:             b.radius = rng.range(9000.0f, 16000.0f);  b.noise = b.radius * rng.range(0.050f, 0.085f); break;
            }
            b.points = clampi((int)(b.radius / 125.0f), 110, 255);
            // Bigger worlds pull harder, but never so hard the ship (thrust
            // 0.15) can't climb away.
            b.gSurface = 0.1f * powf(b.radius / 32000.0f, 0.6f) * rng.range(0.85f, 1.15f);
            if (b.gSurface > 0.105f) b.gSurface = 0.105f;
            float moonChance = b.arch == Arch::Barren ? 0.15f : (b.radius < 16000.0f ? 0.30f : 0.55f);
            b.hasMoon = rng.chance(moonChance);
            if (b.hasMoon) {
                b.moonRadius = rng.range(3500.0f, 7500.0f);
                float skyTop = b.radius * 1.5f;
                float orbit = b.radius * rng.range(3.1f, 3.7f);
                b.moonOrbit = orbit > skyTop * 2.0f ? orbit : skyTop * 2.0f;
            }
        }
        b.reach = b.hasMoon ? b.moonOrbit + b.moonRadius * 2.0f : b.radius * 1.6f;
    }

    // The shaders handle SHADER_MAX_PLANETS (10) bodies including the star:
    // trim moons from the outside in until the system fits.
    auto bodyCount = [&]() {
        int n = 1 + planetCount;
        for (const BodySpec& b : specs) n += b.hasMoon ? 1 : 0;
        return n;
    };
    for (int i = planetCount - 1; i >= 0 && bodyCount() > 10; i--) {
        if (i != homeSlot && specs[i].hasMoon) {
            specs[i].hasMoon = false;
            specs[i].reach = specs[i].radius * 1.6f;
        }
    }

    // ---- Orbit radii --------------------------------------------------------
    std::vector<float> orbit(planetCount);
    float sunRadius = rng.range(36000.0f, 44000.0f);
    orbit[0] = hasInner ? rng.range(170000.0f, 200000.0f) : rng.range(290000.0f, 330000.0f);
    for (int i = 1; i < planetCount; i++) {
        float gap = rng.range(210000.0f, 290000.0f);
        float need = specs[i - 1].reach + specs[i].reach + 50000.0f;
        orbit[i] = orbit[i - 1] + (gap > need ? gap : need);
    }

    // Star mass from the home world's orbital speed: fast enough that the home
    // day (= its year; planets don't spin) turns over in roughly ten minutes,
    // slow enough that matching velocity with a neighbor is a short burn.
    float homeSpeed = rng.range(54.0f, 62.0f);                 // world units / frame
    float sunGM = homeSpeed * homeSpeed * orbit[homeSlot];     // v^2 r

    // ---- Build the bodies ---------------------------------------------------
    planets.clear();
    planets.reserve(10);

    {
        Planet sun;
        sun.isSun = true;
        sun.atmosphere = false;
        sun.name = sys.starName;
        // Star color class (corona tint); most systems get a familiar yellow sun.
        float roll = rng.next01();
        if (roll < 0.55f)      { sun.kind = "Yellow star"; set3(sun.look.sig, 1.0f, 0.86f, 0.39f); }
        else if (roll < 0.75f) { sun.kind = "Orange star"; set3(sun.look.sig, 1.0f, 0.64f, 0.30f); }
        else if (roll < 0.90f) { sun.kind = "White star";  set3(sun.look.sig, 0.80f, 0.88f, 1.0f); }
        else                   { sun.kind = "Red star";    set3(sun.look.sig, 1.0f, 0.44f, 0.30f); }
        sun.gmRef = sunGM;
        sun.discovered = true;
        sun.generate(Vec2(0, 0), sunRadius, 500.0f, 64, G0, rng.nextSeed());
        sun.applyGlobalDensity(G0);
        planets.push_back(sun);
    }
    sys.sunIdx = 0;

    for (int i = 0; i < planetCount; i++) {
        const BodySpec& b = specs[i];
        Planet p;
        TerrainStyle style;
        applyArchetype(p, b.arch, rng, false, style);
        if (i == homeSlot) {
            // The ship drops in at -90 degrees (Lander::spawnOnPlanet) and the
            // herd grazes right there: no mesa on top of either.
            style.keepClearAngle = -90.0f;
            style.keepClearHalf = 2.5f;
        }
        p.name = makeName(rng, names);
        p.densityRel = (b.gSurface * Planet::GRAVITY_REFERENCE_RADIUS / b.radius) / G0;
        p.generate(Vec2(0, 0), b.radius, b.noise, b.points, p.densityRel * G0, rng.nextSeed(), style);
        // Start the planets fanned out on the home world's side of the star
        // (home at +90 degrees: the sun stands at noon over the spawn point),
        // so the first hops are short. They drift apart as the years differ.
        float angle = 90.0f;
        if (i != homeSlot) {
            float side = ((i - homeSlot) & 1) ? 1.0f : -1.0f;
            angle += side * rng.range(8.0f, 38.0f);
        }
        p.setOrbit(sys.sunIdx, orbit[i], 0.0f, angle);
        int planetIdx = (int)planets.size();
        if (i == homeSlot) {
            sys.homeIdx = planetIdx;
            p.discovered = true;
        }
        planets.push_back(p);

        if (b.hasMoon) {
            Planet m;
            TerrainStyle ms;
            applyArchetype(m, Arch::Barren, rng, true, ms);
            m.name = makeName(rng, names);
            m.densityRel = rng.range(0.9f, 1.4f);
            float noise = b.moonRadius * rng.range(0.07f, 0.11f);
            int pts = clampi((int)(b.moonRadius / 40.0f), 110, 180);
            m.generate(Vec2(0, 0), b.moonRadius, noise, pts, m.densityRel * G0, rng.nextSeed(), ms);
            m.setOrbit(planetIdx, b.moonOrbit, 0.0f, rng.range(0.0f, 360.0f));
            planets.push_back(m);
        }
    }

    updateKeplerSpeeds(planets);
    // Settle every body onto its starting orbit position (parents first).
    for (Planet& p : planets) p.updateOrbit(planets, 0.0f);
    return sys;
}
