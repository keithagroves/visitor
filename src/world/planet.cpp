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
    // Remap this noise field to roughly 0..1 to match p5's noise() output.
    auto noise01 = [&perlin](float x) {
        float n = perlin.noise(x, 0.0f) * 0.5f + 0.5f;
        return n < 0 ? 0 : (n > 1 ? 1 : n);
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

    landscape.clear();
    float noiseOffset = rng.range(0.0f, 1000.0f);
    for (int i = 0; i < numPoints; i++) {
        float angle = angleAt((float)i / numPoints);
        float w = rangeWeight(angle);
        float amp = noiseIntensity * (1.0f + w * 0.6f);
        float r = baseRadius + noise01(noiseOffset) * amp;
        noiseOffset += 0.1f;
        landscape.push_back({angle, r});
    }
    // Close the loop.
    if (!landscape.empty()) landscape.push_back(landscape.front());

    // Background ridge layers (ported from planet.js generateBackgroundRidge):
    // uniform-angle silhouettes with smaller amplitude than the main terrain so
    // they mostly hide behind it, poking above only as distant horizon peaks.
    // Far layer first; tuning (follow/lift/alpha/skyMix) carried over 1:1.
    ridges.clear();
    struct RidgeSpec { float ampScale, noiseStep, seedBias, follow, liftScale, alpha, skyMix; };
    const RidgeSpec specs[] = {
        {0.50f, 0.035f, 900.0f, 0.68f, 0.13f, 125.0f / 255.0f, 0.76f},
        {0.82f, 0.055f, 500.0f, 0.84f, 0.07f, 205.0f / 255.0f, 0.55f},
    };
    for (const RidgeSpec& s : specs) {
        Ridge layer;
        layer.follow = s.follow;
        layer.lift = noiseIntensity * s.liftScale;
        layer.alpha = s.alpha;
        layer.skyMix = s.skyMix;
        // 3x the main terrain's resolution: finer silhouette detail without
        // affecting collision/landing (which read the field LUT).
        int count = numPoints * 3;
        float ridgeOffset = rng.range(0.0f, 1000.0f) + s.seedBias;
        float amplitude = noiseIntensity * s.ampScale;
        layer.points.reserve(count + 1);
        for (int i = 0; i < count; i++) {
            float angle = i * (360.0f / count);
            layer.points.push_back({angle, baseRadius + noise01(ridgeOffset) * amplitude});
            ridgeOffset += s.noiseStep;
        }
        layer.points.push_back(layer.points.front());
        ridges.push_back(std::move(layer));
    }

    // Seed the destructible crust from the silhouette, then bake the surface LUT.
    float coreR = baseRadius - SHELL_DEPTH;
    if (coreR < baseRadius * 0.25f) coreR = baseRadius * 0.25f;
    float outerR = maxSurfaceRadius() + CELL * 2.0f;
    const int SEED_N = 4096;
    std::vector<float> seedLUT(SEED_N);
    for (int k = 0; k < SEED_N; k++) seedLUT[k] = seedRadius(k * (360.0f / SEED_N));
    field.generate(center, coreR, outerR, CELL, seedLUT);
    rebuildSurfaceLUT();
}

void Planet::rebuildSurfaceLUT() {
    surfaceLUT.resize(LUT_N);
    for (int k = 0; k < LUT_N; k++)
        surfaceLUT[k] = field.outerSurfaceRadius(k * (360.0f / LUT_N));
}

bool Planet::dig(Vec2 world, float radius) {
    if (!field.dig(world, radius)) return false;
    rebuildSurfaceLUT();
    return true;
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
    float dx = x - center.x;
    float dy = y - center.y;
    float d = sqrtf(dx * dx + dy * dy);
    float altitude = d - baseRadius;
    float thickness = baseRadius * gDebug.atmosphereScale;
    if (altitude >= thickness) return 0.0f;
    if (altitude <= 0) return 1.0f;
    return 1.0f - altitude / thickness;
}
