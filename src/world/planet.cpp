#include "planet.h"
#include <cmath>

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
}

float Planet::surfaceRadius(float angle) const {
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

void Planet::draw(Renderer2D& r) const {
    // The sky/atmosphere is now drawn entirely by the atmosphere shader pass
    // (a smooth gradient with day/night + Rayleigh), so the body just draws the
    // terrain silhouette here.
    // --- Planet body: fan the star-shaped terrain silhouette from the center. ---
    std::vector<Vec2> pts;
    pts.reserve(landscape.size());
    for (const auto& p : landscape) {
        pts.push_back(Vec2(center.x + p.r * cosd(p.angle),
                           center.y + p.r * sind(p.angle)));
    }
    r.fill(110.0f / 255.0f, 105.0f / 255.0f, 100.0f / 255.0f, 1.0f);
    r.fanPolygon(center.x, center.y, pts.data(), (int)pts.size(), false);
}
