#pragma once

#include <vector>
#include "../math/vecmath.h"
#include "../math/noise.h"
#include "../graphics/renderer2d.h"
#include "../core/debug.h"

// A procedurally generated planet body, ported from cow-abductor's planet.js.
// Owns a star-shaped terrain silhouette (radius varies with angle via Perlin
// noise + a couple of "mountain range" clusters), a 1/r^2 gravity field, and a
// soft atmosphere shell. Distances are in the same large world units as the
// original game (a starter planet is ~32000 units across).
class Planet {
public:
    // Tuning carried over from DEBUG in the original game.
    static constexpr float GRAVITY_REFERENCE_RADIUS = 8000.0f;

    struct LandPoint {
        float angle;  // degrees
        float r;      // radius from center
    };

    void generate(Vec2 center, float baseRadius, float noiseIntensity,
                  int numPoints, float density, unsigned int seed);

    // GM term; surface gravity scales with size (see planet.js setDensity).
    void setDensity(float density);

    // Surface radius at a given angle (degrees), interpolated along the
    // silhouette. Mirrors getSurfaceRadius() in sketch.js.
    float surfaceRadius(float angleDeg) const;
    float maxSurfaceRadius() const;

    // 0..1 atmospheric density at a world point (1 at surface, 0 at the top).
    float atmosphericDensity(float x, float y) const;
    float atmosphereOuterRadius() const { return baseRadius * (1.0f + gDebug.atmosphereScale); }

    void draw(Renderer2D& r) const;

    Vec2 center;
    float baseRadius = 0;
    float noiseIntensity = 0;
    float gravity = 0;       // GM term used by every gravity consumer
    float density = 0;

    std::vector<LandPoint> landscape;  // closed loop (last == first)
};
