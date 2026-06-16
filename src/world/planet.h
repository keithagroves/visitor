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
//
// Planets live in a solar system (std::vector<Planet> in main.cpp): each can
// orbit a parent body kinematically, and the heavy destructible TerrainField is
// LAZY — only built (ensureField) for the planet the player is near, released
// when they leave, with dig operations recorded and replayed so craters
// persist across unload/reload.
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
    // surfaceLUT — rebuilt from the destructible field when one is live, or
    // from the analytic silhouette otherwise — so collision, landing, and the
    // shader silhouette all reflect craters dug from the surface.
    float surfaceRadius(float angleDeg) const;
    float maxSurfaceRadius() const;

    // Carve rock out of the destructible crust at a world point, then refresh
    // the surface LUT. Returns true if anything changed; callers then re-upload
    // the field texture region (field.digRect()) and the silhouette LUT.
    // Records the op (in planet-local coords) so it survives field reloads.
    bool dig(Vec2 world, float radius);

    // ---- Lazy destructible field ------------------------------------------
    // Build the density grid (seeded from the silhouette, past digs replayed).
    // Call when the player approaches; release when they leave. surfaceRadius()
    // works either way.
    void ensureField();
    void releaseField();
    bool fieldReady() const { return fieldReady_; }

    // ---- Orbit (kinematic circular, ported from planet.js) -----------------
    // parentIdx indexes the system's planet vector; -1 = pinned in place.
    // updateOrbit recomputes center from the parent and shifts the live field
    // along; orbitVel is the per-frame-unit world motion (inherit on liftoff).
    void setOrbit(int parentIdx, float radius, float speedDegPerFrame, float startAngleDeg);
    void updateOrbit(const std::vector<Planet>& planets, float timeScale);

    // 0..1 atmospheric density at a world point (1 at surface, 0 at the top).
    // Always 0 on airless bodies (atmosphere == false).
    float atmosphericDensity(float x, float y) const;
    float atmosphereOuterRadius() const {
        return atmosphere ? baseRadius * (1.0f + gDebug.atmosphereScale) : baseRadius;
    }

    Vec2 center;
    float baseRadius = 0;
    float noiseIntensity = 0;
    float gravity = 0;       // GM term used by every gravity consumer
    float density = 0;

    bool isSun = false;      // drawn as a glowing star; excluded from body/sky passes
    bool atmosphere = true;  // false = airless body (no sky, no clouds, no drag)

    int orbitParent = -1;    // index into the system vector; -1 = pinned
    float orbitRadius = 0;
    float orbitSpeed = 0;    // degrees per frame-unit
    float orbitAngle = 0;    // degrees
    Vec2 orbitVel;           // world units per frame-unit (last updateOrbit)

    std::vector<LandPoint> landscape;  // closed loop (last == first); seeds the field
    TerrainField field;                // destructible crust (SDF density grid), lazy

    // Distant mountain silhouettes drawn behind the terrain (ported from the
    // original's generateBackgroundRidge). Each layer follows a fraction of the
    // player's surface angle so the horizon slides with depth as you fly.
    struct Ridge {
        std::vector<LandPoint> points; // closed loop (last == first)
        float follow;                  // fraction of the view angle the layer tracks
        float lift;                    // radial offset so ridges sit above valleys
        float alpha;                   // layer opacity (0..1)
        float skyMix;                  // how far the tint fades toward sky haze
        float tintR, tintG, tintB;     // base hill color before haze/shading
        float topR;                    // max crest radius incl. lift (gradient anchor)
        bool foreground = false;       // drawn OVER the terrain (near framing plane)
                                       // instead of behind it (distant haze layers)
    };
    std::vector<Ridge> ridges;         // far-to-near order (background, then foreground)

    // Silhouette clutter that sells near-camera depth: foreground props (spires
    // / fronds standing on the near framing plane, drawn closest and swaying)
    // and ground tufts (small clumps sitting on the real surface). Placed once
    // at generate(); drawn by main's drawProps. kind: 0 = spire, 1 = frond.
    struct Prop {
        float angle;       // degrees around the planet
        float scale;       // size multiplier
        float phase;       // sway phase offset
        float follow;      // parallax follow of the plane it lives on
        float radialFrac;  // foreground props: height above base as frac of noiseIntensity
        int   kind;
        bool  foreground;  // true = near framing plane; false = on the ground surface
    };
    std::vector<Prop> props;

private:
    // The original star-shaped silhouette, interpolated from `landscape`. Used
    // to seed the field and as the LUT source while no field is live.
    float seedRadius(float angleDeg) const;
    void rebuildSurfaceLUT();
    static constexpr int LUT_N = 2048;
    std::vector<float> surfaceLUT;     // angle (uniform) -> outer surface radius

    bool fieldReady_ = false;
    struct DigOp { Vec2 local; float radius; };  // planet-local, replayed on reload
    std::vector<DigOp> digOps_;
};
