#include "planet.h"
#include <cmath>

// Crust tuning: the destructible shell spans [baseRadius - SHELL_DEPTH .. peaks];
// everything below is the always-solid core. CELL is the dig/mesh granularity.
static constexpr float SHELL_DEPTH = 9000.0f;
static constexpr float CELL = 250.0f;

namespace {
// Small deterministic RNG so each planet's terrain is stable for a given seed
// (stands in for the original's reliance on a globally seeded noise field).
struct Rng {
    unsigned int state;
    explicit Rng(unsigned int s) : state(s ? s : 1) {}
    float next01() {
        state = state * 1664525u + 1013904223u;
        return (state >> 8) * (1.0f / 16777216.0f); // 24-bit mantissa -> [0,1)
    }
    float range(float a, float b) { return a + next01() * (b - a); }
};
}

void Planet::setDensity(float d) {
    density = d;
    gravity = d * (baseRadius * baseRadius * baseRadius) / GRAVITY_REFERENCE_RADIUS;
}

void Planet::generate(Vec2 c, float radius, float noiseIntensityIn,
                      int numPoints, float densityIn, unsigned int seed) {
    center = c;
    baseRadius = radius;
    noiseIntensity = noiseIntensityIn;
    setDensity(densityIn);

    Rng rng(seed);
    PerlinNoise perlin(seed);
    // Seamless noise around the silhouette: sample 2D Perlin on a circle of
    // radius `freq` so angle 0 and 360 meet exactly (the old linear walk left a
    // small cliff at the seam). Returns roughly [-1, 1].
    auto circNoise = [&perlin](float angleDeg, float freq, float ox, float oy) {
        float a = angleDeg * (3.14159265f / 180.0f);
        return perlin.noise(cosf(a) * freq + ox, sinf(a) * freq + oy);
    };

    // Per-octave decorrelation offsets, fixed for this planet's seed.
    float octOx[6], octOy[6];
    for (int o = 0; o < 6; o++) {
        octOx[o] = rng.range(-100.0f, 100.0f);
        octOy[o] = rng.range(-100.0f, 100.0f);
    }

    // Rolling fBm: gentle 3-octave hills for the basins. ~[0, 1].
    auto rollingFbm = [&](float angleDeg) {
        float sum = 0, amp = 1.0f, freq = 1.6f, norm = 0;
        for (int o = 0; o < 3; o++) {
            sum += (circNoise(angleDeg, freq, octOx[o], octOy[o]) * 0.5f + 0.5f) * amp;
            norm += amp;
            amp *= 0.5f;
            freq *= 2.0f;
        }
        return sum / norm;
    };

    // Ridged multifractal (Musgrave): fold the noise at its zero crossings
    // (1 - |n|) into sharp V-creases, square to pinch the summits, and weight
    // each octave by the one below it so detail piles onto crests while
    // valley floors stay smooth. This is the same construction the classic
    // 3D heightmap mountains use, evaluated around the circle. ~[0, 1].
    auto ridgedFbm = [&](float angleDeg) {
        float sum = 0, amp = 0.55f, freq = 2.2f, w = 1.0f, norm = 0;
        for (int o = 0; o < 5; o++) {
            float p = circNoise(angleDeg, freq, octOx[o], octOy[o]);
            float r = 1.0f - std::fabs(p) * 1.6f;
            if (r < 0) r = 0;
            r = r * r * w;
            w = r * 1.8f;
            if (w > 1) w = 1;
            sum += r * amp;
            norm += amp;
            amp *= 0.5f;
            freq *= 2.15f;
        }
        return sum / norm;
    };

    // 1-2 mountain "range" centers. Angles near these cluster more sample
    // points (sharper detail) and get taller peaks.
    int numRanges = (int)std::floor(rng.range(1.0f, 3.0f));
    struct Range { float center, width, intensity; };
    std::vector<Range> ranges;
    for (int i = 0; i < numRanges; i++) {
        ranges.push_back({rng.range(0.0f, 360.0f), rng.range(14.0f, 22.0f), rng.range(2.5f, 4.0f)});
    }
    auto rangeWeight = [&ranges](float theta) {
        float w = 0;
        for (const auto& r : ranges) {
            float delta = std::fmod(theta - r.center + 540.0f, 360.0f) - 180.0f;
            w += r.intensity * std::exp(-(delta * delta) / (2.0f * r.width * r.width));
        }
        return w;
    };

    // Inverse-CDF angle sampling so clustered regions get more points while the
    // polygon stays monotonic in angle (can't self-intersect).
    const int BUCKETS = 720;
    std::vector<float> cumulative(BUCKETS + 1, 0.0f);
    for (int i = 0; i < BUCKETS; i++) {
        float theta = (i + 0.5f) * (360.0f / BUCKETS);
        cumulative[i + 1] = cumulative[i] + 1.0f + rangeWeight(theta);
    }
    float totalMass = cumulative[BUCKETS];
    auto angleAt = [&](float t) {
        float target = t * totalMass;
        int lo = 0, hi = BUCKETS;
        while (lo < hi) {
            int mid = (lo + hi) >> 1;
            if (cumulative[mid] < target) lo = mid + 1;
            else hi = mid;
        }
        int idx = lo > 0 ? lo - 1 : 0;
        float frac = (target - cumulative[idx]) / (cumulative[idx + 1] - cumulative[idx]);
        return (idx + frac) * (360.0f / BUCKETS);
    };

    // Height profile: rolling fBm in the basins, ridged multifractal inside the
    // range clusters (blended by rangeWeight), with the ranges also getting an
    // amplitude boost. Ridged peaks push altFrac toward 1, which is where the
    // body shader's snow band lives — so the high ranges cap with snow while
    // the basins stay in the dark tones.
    landscape.clear();
    for (int i = 0; i < numPoints; i++) {
        float angle = angleAt((float)i / numPoints);
        float w = rangeWeight(angle);
        float mountain = w / (w + 0.8f);   // 0 in basins -> ~1 at range centers
        float h = rollingFbm(angle) * (1.0f - mountain) + ridgedFbm(angle) * mountain;
        float amp = noiseIntensity * (1.0f + w * 0.6f);
        landscape.push_back({angle, baseRadius + h * amp});
    }
    // Close the loop.
    if (!landscape.empty()) landscape.push_back(landscape.front());

    // Background ridge layers (ported from planet.js generateBackgroundRidge):
    // uniform-angle silhouettes with smaller amplitude than the main terrain so
    // they mostly hide behind it, poking above only as distant horizon peaks.
    // Far layer first; tuning (follow/lift/alpha/skyMix) carried over 1:1.
    // Only worlds with air get hazy horizons (set atmosphere/isSun BEFORE
    // calling generate).
    ridges.clear();
    if (atmosphere && !isSun) {
        struct RidgeSpec {
            float ampScale, noiseStep, seedBias, follow, liftScale, alpha, skyMix;
            float tintR, tintG, tintB;
            bool fg;   // foreground framing plane (drawn OVER the terrain)
        };
        // Far-to-near. Amplitudes/steps/haze carry the p5 tuning; the lifts are
        // raised from the original because our spawn terrain isn't flattened to
        // the valley floor like p5's landing pads were — without the lift the
        // ranges hide below the local ground and the horizon reads empty.
        //
        // Six BACKGROUND layers form a smooth atmospheric-perspective ramp
        // (Summer Afternoon-style): far ranges are pale lavender, high-follow
        // (slide slowest), nearly dissolved into the sky haze; near ranges are
        // warm/dark, low-follow (slide faster), crisp. Stacking many planes at
        // a smooth depth gradient is what reads as a deep 3D scene rather than
        // a flat horizon.
        //
        // follow: fraction of the player's angle the layer tracks. HIGHER =
        // moves less relative to the camera = reads farther away. So the far
        // hazy layer gets the biggest follow and the near layer the smallest.
        //
        // The final entry (fg = true) is a single dark, fast (low-follow),
        // chunky FOREGROUND plane drawn OVER the terrain — the near framing
        // silhouette that passes across the bottom of the frame and shoves
        // everything behind it into depth (the Dave-the-Diver move).
        const RidgeSpec specs[] = {
            {0.26f, 0.018f, 1700.0f, 0.91f, 0.58f, 110.0f / 255.0f, 0.93f, 0.66f, 0.66f, 0.78f, false},
            {0.33f, 0.024f, 1400.0f, 0.84f, 0.48f, 135.0f / 255.0f, 0.85f, 0.60f, 0.60f, 0.72f, false},
            {0.43f, 0.032f, 1150.0f, 0.76f, 0.39f, 165.0f / 255.0f, 0.74f, 0.54f, 0.53f, 0.63f, false},
            {0.55f, 0.041f,  900.0f, 0.67f, 0.30f, 195.0f / 255.0f, 0.61f, 0.49f, 0.48f, 0.55f, false},
            {0.68f, 0.050f,  680.0f, 0.58f, 0.22f, 222.0f / 255.0f, 0.47f, 0.43f, 0.44f, 0.48f, false},
            {0.84f, 0.058f,  460.0f, 0.49f, 0.15f, 242.0f / 255.0f, 0.34f, 0.39f, 0.42f, 0.44f, false},
            // Foreground framing planes — dark, low-follow, big chunky lumps.
            // Two of them at different speeds so the near edge reads as THICK,
            // not a single cutout: a mid-dark one, then a closer/darker/faster
            // one right at the camera.
            {0.62f, 0.019f,  220.0f, 0.30f, 0.06f, 255.0f / 255.0f, 0.00f, 0.17f, 0.18f, 0.22f, true},
            {0.80f, 0.014f,   60.0f, 0.20f, 0.02f, 255.0f / 255.0f, 0.00f, 0.09f, 0.10f, 0.13f, true},
        };
        for (const RidgeSpec& s : specs) {
            Ridge layer;
            layer.foreground = s.fg;
            layer.follow = s.follow;
            layer.lift = noiseIntensity * s.liftScale;
            layer.alpha = s.alpha;
            layer.skyMix = s.skyMix;
            layer.tintR = s.tintR;
            layer.tintG = s.tintG;
            layer.tintB = s.tintB;
            // 3x the main terrain's resolution: finer silhouette detail without
            // affecting collision/landing (which read the field LUT).
            int count = numPoints * 3;
            float amplitude = noiseIntensity * s.ampScale;
            // Match the old walk's bump count: it covered count*noiseStep noise
            // units per revolution; the circle sampler covers 2*pi*freq.
            float baseFreq = count * s.noiseStep / 6.2831853f;
            float lOx[3], lOy[3];
            for (int o = 0; o < 3; o++) {
                lOx[o] = rng.range(-100.0f, 100.0f) + s.seedBias;
                lOy[o] = rng.range(-100.0f, 100.0f);
            }
            layer.points.reserve(count + 1);
            for (int i = 0; i < count; i++) {
                float angle = i * (360.0f / count);
                // 3-octave ridged fBm so the horizon reads as peaks, not swells.
                float sum = 0, amp = 1.0f, freq = baseFreq, wgt = 1.0f, norm = 0;
                for (int o = 0; o < 3; o++) {
                    float p = circNoise(angle, freq, lOx[o], lOy[o]);
                    float r = 1.0f - std::fabs(p) * 1.6f;
                    if (r < 0) r = 0;
                    r = r * r * wgt;
                    wgt = r * 1.8f;
                    if (wgt > 1) wgt = 1;
                    sum += r * amp;
                    norm += amp;
                    amp *= 0.5f;
                    freq *= 2.1f;
                }
                layer.points.push_back({angle, baseRadius + (sum / norm) * amplitude});
            }
            layer.points.push_back(layer.points.front());
            layer.topR = baseRadius + layer.lift;
            for (const LandPoint& pt : layer.points)
                if (pt.r + layer.lift > layer.topR) layer.topR = pt.r + layer.lift;
            ridges.push_back(std::move(layer));
        }

        // Silhouette clutter: foreground props on the near framing plane plus
        // ground tufts scattered on the surface. Spread evenly around the
        // planet with jitter so flying reveals a steady stream of them.
        props.clear();
        const float nearFollow = 0.20f;  // matches the closest foreground ridge
        int fgCount = 220;
        for (int i = 0; i < fgCount; i++) {
            Prop pr{};
            pr.foreground = true;
            pr.angle = (float)i / fgCount * 360.0f + rng.range(-0.6f, 0.6f);
            pr.scale = rng.range(0.7f, 1.8f);
            pr.phase = rng.range(0.0f, 6.2831853f);
            pr.follow = nearFollow;
            pr.radialFrac = rng.range(0.02f, 0.14f);   // low, at the bottom edge
            pr.kind = rng.range(0.0f, 1.0f) < 0.5f ? 0 : 1;
            props.push_back(pr);
        }
        int tuftCount = 320;
        for (int i = 0; i < tuftCount; i++) {
            Prop pr{};
            pr.foreground = false;
            pr.angle = (float)i / tuftCount * 360.0f + rng.range(-0.5f, 0.5f);
            pr.scale = rng.range(0.5f, 1.2f);
            pr.phase = rng.range(0.0f, 6.2831853f);
            pr.follow = 0.0f;        // no parallax shift: pinned to the surface
            pr.radialFrac = 0.0f;
            pr.kind = 1;             // small fronds/tufts
            props.push_back(pr);
        }
    }

    // The destructible field is LAZY (built on approach via ensureField); until
    // then the surface LUT comes straight from the analytic silhouette.
    fieldReady_ = false;
    digOps_.clear();
    rebuildSurfaceLUT();
}

void Planet::ensureField() {
    if (fieldReady_) return;
    float coreR = baseRadius - SHELL_DEPTH;
    if (coreR < baseRadius * 0.25f) coreR = baseRadius * 0.25f;
    float outerR = maxSurfaceRadius() + CELL * 2.0f;
    const int SEED_N = 4096;
    std::vector<float> seedLUT(SEED_N);
    for (int k = 0; k < SEED_N; k++) seedLUT[k] = seedRadius(k * (360.0f / SEED_N));
    field.generate(center, coreR, outerR, CELL, seedLUT);
    // Replay recorded digs (stored planet-local) so craters survive reloads.
    for (const DigOp& op : digOps_)
        field.dig(Vec2(center.x + op.local.x, center.y + op.local.y), op.radius);
    fieldReady_ = true;
    rebuildSurfaceLUT();
}

void Planet::releaseField() {
    if (!fieldReady_) return;
    field.release();
    fieldReady_ = false;
    rebuildSurfaceLUT();   // falls back to the analytic silhouette + nothing else
}

void Planet::rebuildSurfaceLUT() {
    surfaceLUT.resize(LUT_N);
    if (fieldReady_) {
        for (int k = 0; k < LUT_N; k++)
            surfaceLUT[k] = field.outerSurfaceRadius(k * (360.0f / LUT_N));
    } else {
        for (int k = 0; k < LUT_N; k++)
            surfaceLUT[k] = seedRadius(k * (360.0f / LUT_N));
    }
}

bool Planet::dig(Vec2 world, float radius) {
    if (!fieldReady_) return false;   // can only dig the planet you're standing on
    if (!field.dig(world, radius)) return false;
    digOps_.push_back({Vec2(world.x - center.x, world.y - center.y), radius});
    rebuildSurfaceLUT();
    return true;
}

void Planet::setOrbit(int parentIdx, float radius, float speedDegPerFrame, float startAngleDeg) {
    orbitParent = parentIdx;
    orbitRadius = radius;
    orbitSpeed = speedDegPerFrame;
    orbitAngle = startAngleDeg;
}

void Planet::updateOrbit(const std::vector<Planet>& planets, float timeScale) {
    if (orbitParent < 0 || orbitParent >= (int)planets.size()) {
        orbitVel = Vec2(0, 0);
        return;
    }
    orbitAngle += orbitSpeed * timeScale;
    const Vec2& pc = planets[orbitParent].center;
    Vec2 newCenter(pc.x + orbitRadius * cosd(orbitAngle),
                   pc.y + orbitRadius * sind(orbitAngle));
    orbitVel = timeScale > 1e-6f
                   ? Vec2((newCenter.x - center.x) / timeScale, (newCenter.y - center.y) / timeScale)
                   : Vec2(0, 0);
    center = newCenter;
    if (fieldReady_) field.moveTo(center);   // grid is center-relative; pure shift
}

float Planet::surfaceRadius(float angle) const {
    int n = (int)surfaceLUT.size();
    if (n == 0) return baseRadius;
    while (angle < 0.0f) angle += 360.0f;
    while (angle >= 360.0f) angle -= 360.0f;
    float f = angle / 360.0f * n;
    int i0 = (int)f % n;
    int i1 = (i0 + 1) % n;
    float frac = f - (float)((int)f);
    return surfaceLUT[i0] * (1.0f - frac) + surfaceLUT[i1] * frac;
}

float Planet::seedRadius(float angle) const {
    if (landscape.size() < 2) return baseRadius;
    if (angle < 0) angle += 360.0f;
    for (size_t i = 0; i + 1 < landscape.size(); i++) {
        const LandPoint& cur = landscape[i];
        const LandPoint& next = landscape[i + 1];
        float curA = cur.angle;
        float nextA = next.angle < curA ? next.angle + 360.0f : next.angle;
        float adjA = angle < curA ? angle + 360.0f : angle;
        if (adjA >= curA && adjA <= nextA) {
            float f = (adjA - curA) / (nextA - curA);
            return cur.r + (next.r - cur.r) * f;
        }
    }
    return baseRadius;
}

float Planet::maxSurfaceRadius() const {
    float maxR = baseRadius;
    for (const auto& p : landscape) {
        if (p.r > maxR) maxR = p.r;
    }
    return maxR;
}

float Planet::atmosphericDensity(float x, float y) const {
    if (!atmosphere) return 0.0f;
    float dx = x - center.x;
    float dy = y - center.y;
    float d = sqrtf(dx * dx + dy * dy);
    float altitude = d - baseRadius;
    float thickness = baseRadius * gDebug.atmosphereScale;
    if (altitude >= thickness) return 0.0f;
    if (altitude <= 0) return 1.0f;
    return 1.0f - altitude / thickness;
}
