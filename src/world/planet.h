#pragma once

#include <vector>
#include "../math/vecmath.h"
#include "../math/noise.h"
#include "../graphics/renderer2d.h"
#include "../core/debug.h"
#include "terrain_field.h"

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

    // Outermost surface radius at a given angle (degrees). Reads the fast CPU
    // surfaceLUT (rebuilt from the destructible field), so collision, landing,
    // and the shader silhouette all reflect craters dug from the surface.
    float surfaceRadius(float angleDeg) const;
    float maxSurfaceRadius() const;

    // Carve rock out of the destructible crust at a world point, then refresh
    // the surface LUT. Returns true if anything changed; callers then re-upload
    // the field texture region (field.digRect()) and the silhouette LUT.
    bool dig(Vec2 world, float radius);

    // 0..1 atmospheric density at a world point (1 at surface, 0 at the top).
    float atmosphericDensity(float x, float y) const;
    float atmosphereOuterRadius() const { return baseRadius * (1.0f + gDebug.atmosphereScale); }

    Vec2 center;
    float baseRadius = 0;
    float noiseIntensity = 0;
    float gravity = 0;       // GM term used by every gravity consumer
    float density = 0;

    std::vector<LandPoint> landscape;  // closed loop (last == first); seeds the field
    TerrainField field;                // destructible crust (SDF density grid)

    // Distant mountain silhouettes drawn behind the terrain (ported from the
    // original's generateBackgroundRidge). Each layer follows a fraction of the
    // player's surface angle so the horizon slides with depth as you fly.
    struct Ridge {
        std::vector<LandPoint> points; // closed loop (last == first)
        float follow;                  // fraction of the view angle the layer tracks
        float lift;                    // radial offset so ridges sit above valleys
        float alpha;                   // layer opacity (0..1)
        float skyMix;                  // how far the rock tint fades toward sky haze
    };
    std::vector<Ridge> ridges;         // far-to-near order

private:
    // The original star-shaped silhouette, interpolated from `landscape`. Used
    // only to seed the field; gameplay reads surfaceRadius() (the field LUT).
    float seedRadius(float angleDeg) const;
    void rebuildSurfaceLUT();
    static constexpr int LUT_N = 2048;
    std::vector<float> surfaceLUT;     // angle (uniform) -> outer surface radius
};
