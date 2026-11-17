#pragma once

#include "../math/vecmath.h"
#include "../graphics/renderer2d.h"
#include "../world/planet.h"

// The player on foot: walks the surface, jumps, jetpacks, digs. Simulated in
// the co-moving frame of the body it stands on (the caller carries it along
// with the planet's orbit). Drawn as flat vector shapes — see astronaut.cpp.
class Astronaut {
public:
    static constexpr float RADIUS           = 12.0f;
    // Ground friction per frame. Top running speed and jump take-off speed
    // are live knobs: gDebug.walkSpeed / gDebug.jumpSpeed.
    static constexpr float WALK_DAMP        = 0.85f;
    // Ground covered by one full run cycle (two steps), world units. The legs
    // are driven by distance travelled, so they keep pace with the body at any
    // walk speed instead of pedalling at a fixed rate.
    static constexpr float STRIDE_CYCLE     = 40.0f;
    // Collision against the rock (Planet::rockAt): how close the center may
    // come to it (the feet), and circles standing in for the chest and the
    // helmet. The figure is DRAWN 13 units lower than it physically stands
    // (boots on the walking line, below the terrain line — NearField::
    // WALK_DROP), and dug space is drawn with the same drop; so the physical
    // body these describe sits 13 above the picture: feet at the center,
    // chest around +14, helmet around +27, top of the head at +34.5.
    static constexpr float FOOT_R           = 2.0f;
    static constexpr float BODY_R           = 8.0f;
    static constexpr float BODY_UP          = 14.0f;
    static constexpr float HEAD_R           = 7.5f;
    static constexpr float HEAD_UP          = 27.0f;
    static constexpr float STEP_DOWN        = 7.0f;   // how far a walker reaches down for the floor
    // Steering in the air: up to this share of the walk speed, gained this fast.
    static constexpr float AIR_SPEED        = 0.75f;
    static constexpr float AIR_ACCEL        = 0.10f;  // world units / frame^2
    static constexpr float SCALE            = 0.5f;
    static constexpr float JETPACK_MAX_FUEL = 100.0f;
    static constexpr float JETPACK_THRUST   = 0.25f;
    static constexpr float JETPACK_DRAIN    = 0.55f;
    static constexpr float JETPACK_CHARGE   = 0.55f;

    void spawn(Vec2 position);

    void update(const Planet& planet, float timeScale);
    void walkLeft(const Planet& planet, float timeScale);
    void walkRight(const Planet& planet, float timeScale);
    void move(const Planet& planet, float timeScale, int dir);   // -1 left, +1 right; runs or air-steers
    void jump(const Planet& planet);
    void jetpack(const Planet& planet, float timeScale);

    void draw(Renderer2D& r, const Planet& planet) const;

    Vec2  pos;
    Vec2  vel;
    float walkAnim    = 0.0f;
    float idleAnim    = 0.0f;
    float jetpackFuel = JETPACK_MAX_FUEL;
    int   facing      = 1;
    bool  grounded    = false;
    bool  walking     = false;
    bool  jetting     = false;
    bool  active      = false;
    bool  aiming      = false;   // set by the caller while the tool (laser / cutter) fires
    Vec2  aimWorld;              // what the laser is pointed at (world space)
    // Tip of the mining laser in world space, as last drawn — the caller
    // starts the beam here so it leaves the gun whatever the pose.
    mutable Vec2 muzzleWorld;

    // Pose blends, eased in update() and read by draw(): 0..1 amounts of
    // "running", "off the ground" and "aiming the tool".
    float stride      = 0.0f;
    float air         = 0.0f;
    float aim         = 0.0f;
    float jumpGrace   = 0.0f;    // frames left in which a fresh jump can't be re-grounded

private:
    bool walkRequested = false;
};
