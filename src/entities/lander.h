#pragma once

#include <vector>
#include "../math/vecmath.h"
#include "../graphics/renderer2d.h"
#include "../world/planet.h"
#include "../core/debug.h"

// The player ship. Physics (1/r^2 gravity, thrust along the ship's local "up",
// multiplicative atmospheric drag with substepping, velocity clamp) are ported
// from cow-abductor's lander.js. Gravity and drag sum over every body in the
// system; landing/beam/contact are judged against the dominant body by main.
class Lander {
public:
    // Fixed tuning (the live-tunable ones — thrust, fuel, beam, drag — moved to
    // gDebug; see debug.h).
    static constexpr float TOP_SPEED = 1000.0f;
    static constexpr float SUBSTEP_MAX_KDT = 0.1f;
    static constexpr float SCALE = 0.8f;

    // Landing thresholds, from sketch.js.
    static constexpr float LANDING_MAX_SPEED = 100.0f;
    static constexpr float LANDING_MAX_TILT = 45.0f;   // ship-vs-radial, degrees
    // Afloat (the ship comes down on water as it does on ground, and stays on
    // top): how much lower it sits in the water than it would on land.
    static constexpr float FLOAT_DRAFT = 3.0f;

    void reset();
    void spawnOnPlanet(const Planet& planet, float angle = -90.0f);

    void rotate(float direction);     // direction * 15 deg onto targetRotation
    void setThrust(float power);      // 0..1; ignored when out of fuel

    // Advance one simulation step (pure physics; ground contact is judged by
    // the caller). `timeScale` is frame-relative (1 == one 60fps frame) so the
    // original tuning constants carry over unchanged. No-op once inactive.
    // Gravity sums over every body; drag comes from whichever atmosphere the
    // ship is inside (they never overlap in practice).
    void update(const std::vector<Planet>& planets, float timeScale);

    // State transitions ported from lander.js.
    void land(const Planet& planet);   // touch down: stick to the surface, start settling
    // While landed: ease from where the ship touched down to its resting pose
    // (see land). Call every LANDED frame before reading landingOffset.
    void settle(const Planet& planet, float timeScale);
    // Has a ship that lifted off cleared the ground yet? Until it has (or it
    // starts sinking back), touching the ground is not a new landing.
    bool clearingGround() const { return liftingOff; }
    void updateClearing(const Planet& planet, float altitude);
    void crash();                      // disable controls, mark wrecked
    void liftOff(const Planet& planet);// reactivate, nudge clear, ride the orbit

    // Is the ship's "legs down" axis within LANDING_MAX_TILT of straight-up
    // (away from the planet)?
    bool isUpright(const Planet& planet) const;

    // Recompute the beam cone for this frame (terrain-clipped). Sets beamOn and
    // the triangle A/B/C used both for rendering and for catch tests.
    void updateBeam(const Planet& planet);
    void drawBeam(Renderer2D& r);

    void draw(Renderer2D& r);

    // Gravity from one body / net gravity from the whole system at a point.
    Vec2 gravityAt(const Planet& planet, Vec2 p) const;
    Vec2 gravityAt(const std::vector<Planet>& planets, Vec2 p) const;

    // Unit vector the nose points along (== thrust direction).
    Vec2 noseDir() const { return Vec2(sind(rotation), -cosd(rotation)); }

    // Where the predicted coast path comes closest to a body other than the
    // reference one (body < 0: nothing nearby). Positions are in the same
    // frame as the path, so the caller can draw a "ghost" of the world at the
    // place it will be when the ship arrives.
    struct Encounter {
        int body = -1;
        bool impact = false;   // the path ends on this body's surface
        float distance = 0;    // center-to-ship distance at closest approach
        float score = 0;       // distance in body radii (lower = closer pass)
        Vec2 bodyPos;          // body center at that moment
        Vec2 shipPos;          // ship at that moment
    };

    // Forward-simulate the coast path (gravity + drag, no thrust) into `out`,
    // stopping at terrain impact on any body. The planets are advanced along
    // their orbits during the simulation, and the path is expressed relative
    // to body `frameIdx` (anchored at its current position; -1 = world frame)
    // — i.e. it is the path as seen from the world you are flying around.
    void predictTrajectory(const std::vector<Planet>& planets, int frameIdx,
                           std::vector<Vec2>& out, Encounter& enc) const;

    Vec2 pos;
    Vec2 vel;
    float rotation = -90.0f;
    float targetRotation = -90.0f;
    float fuel = 250.0f;
    float thrusting = 0.0f;     // current commanded thrust 0..1
    float thrustLevel = 0.0f;   // smoothed for rendering
    bool active = true;         // physics + controls live
    bool crashed = false;
    float altitude = 0.0f;      // distance from ship center to terrain (HUD)
    float anim = 0.0f;          // animation timer for flame flicker

    // Tractor beam state (recomputed each frame by updateBeam).
    bool abducting = false;     // player is holding the beam key
    bool beamOn = false;        // beam is actually firing this frame
    Vec2 beamA, beamB, beamC;   // cone triangle (apex at ship, base at ground)
    Vec2 beamEnd;
    Vec2 beamPerp;              // unit axis across the beam (for stripes/dust)

    // Dust motes funneled up the beam; each rides the cone from ground to ship.
    struct Dust { float t; float speed; float off; };
    std::vector<Dust> beamDust;

    // When landed, the ship sticks to the body at this offset from its center
    // (so it rides along once planets orbit in a later phase).
    bool landed = false;
    Vec2 landingOffset;

    // Touchdown settle (see land/settle): how far above its resting depth the
    // ship still is and how fast it is moving there, its leftover slide along
    // the ground (world units / frame), and whether it is still moving at all.
    bool  settling = false;
    float settleHeight = 0.0f;
    float settleSpeed = 0.0f;
    float settleSlide = 0.0f;
    bool  liftingOff = false;
    // Drawn this far from where it is (the caller sets it every frame): the
    // slow rise and fall of a ship riding on water. Zero on land.
    Vec2  bob;

    float radius = 20.0f;
};
