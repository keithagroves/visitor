#pragma once

#include "../math/vecmath.h"
#include "../graphics/renderer2d.h"
#include "../world/planet.h"
#include "livery.h"

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
    static constexpr float JETPACK_DRAIN    = 0.55f;
    static constexpr float JETPACK_CHARGE   = 0.55f;
    // In water (Planet::waterDepth). The suit is heavy, so you sink — slowly:
    // the water carries SWIM_BUOYANCY of your weight once you are in over
    // the chest, and slows everything (SWIM_DRAG is the speed kept per
    // frame, fully in). You walk the bottom as you walk the ground, a little
    // slower; holding jump swims up — a lift of SWIM_LIFT times your weight,
    // easing off as your chest reaches the surface, so held there it treads
    // water with your head out — and holding down dives faster than sinking.
    static constexpr float SWIM_BUOYANCY    = 0.72f;
    static constexpr float SWIM_DRAG        = 0.93f;
    static constexpr float SWIM_LIFT        = 1.15f;
    static constexpr float SWIM_DIVE        = 0.9f;
    static constexpr float WET_FULL         = 20.0f;  // water this far over the feet = fully in
    // Sliding (Shift, or down on dry ground): feet first, nearly no friction,
    // so a slope carries you down it and a run carries on a long way. The
    // friction kept per frame is gDebug.slideGrip; walking does nothing while
    // you slide. Not in water over the shins (SLIDE_WET).
    static constexpr float SLIDE_WET        = 0.35f;

    void spawn(Vec2 position);

    void update(const Planet& planet, float timeScale);
    void walkLeft(const Planet& planet, float timeScale);
    void walkRight(const Planet& planet, float timeScale);
    void move(const Planet& planet, float timeScale, int dir);   // -1 left, +1 right; runs or air-steers
    void jump(const Planet& planet);
    void jetpack(const Planet& planet, float timeScale);
    void swim(const Planet& planet, float timeScale, int dir);   // +1 up, -1 down; nothing out of water

    void draw(Renderer2D& r, const Planet& planet) const;

    Vec2  pos;
    Vec2  vel;
    float walkAnim    = 0.0f;
    float idleAnim    = 0.0f;
    float jetpackFuel = JETPACK_MAX_FUEL;
    // How much the pack holds: JETPACK_MAX_FUEL, more with the jetpack
    // upgrade the traders sell (the caller keeps this up to date).
    float jetpackMax  = JETPACK_MAX_FUEL;
    int   facing      = 1;
    bool  grounded    = false;
    bool  walking     = false;
    bool  jetting     = false;
    bool  active      = false;
    bool  aiming      = false;   // set by the caller while the tool (laser / cutter) fires
    bool  slideHeld   = false;   // set by the caller each frame: the slide is asked for
    bool  sliding     = false;   // ...and we are on the ground sliding (update)
    Vec2  aimWorld;              // what the laser is pointed at (world space)
    // The light sword (main.cpp, updateSword). swordOut: it is the tool in
    // hand: held lit and pointed along swordAng (put away, the hilt hangs at
    // the hip). Sword angles here are degrees from straight down, + toward
    // screen right, so they mean the same whichever way the figure faces.
    // The caller brings swordAng round after the cursor.
    // swing: how far through a swing we are, in units of the cut — 0..1 the
    // cut across the line at swingAng, 1..1+SWORD_BACK the arm coming back
    // to where it is held; < 0 = not swinging. swingDir: +1 the cut comes
    // down across the line, -1 back up it (the second of two quick cuts).
    static constexpr float SWORD_BACK = 0.6f;
    static constexpr float SWORD_REST = 115.0f;   // held with nothing to point at: forward and a little up
    bool  swordOut    = false;
    // How far the blade is out of the hilt: 0 = off .. 1 = full length. Eased
    // in update(): it shoots out when the sword is drawn (SWORD_IGNITE
    // seconds) and slides back in when it is put away (SWORD_RETRACT).
    static constexpr float SWORD_IGNITE  = 0.22f;
    static constexpr float SWORD_RETRACT = 0.15f;
    float blade       = 0.0f;
    float swing       = -1.0f;
    float swordAng    = SWORD_REST;
    // Which way the held blade is drawn pointing (same measure): swordAng,
    // kept in front of the figure — it goes no further round behind than
    // HOLD_TURN from straight ahead, so turning round, the blade comes over
    // the top with us instead of reaching back over the pack.
    static constexpr float HOLD_TURN = 115.0f;
    float swordHeld() const;
    float swingAng    = 90.0f;
    int   swingDir    = 1;
    // Set by the caller while the tool turns us to face what it points at:
    // walking then does not turn us round (walk away from it and you back
    // off, still facing it).
    bool  faceAim     = false;
    Livery livery;               // the player's colour: the suit's trim and lights
    // Tip of the mining laser in world space, as last drawn — the caller
    // starts the beam here so it leaves the gun whatever the pose.
    mutable Vec2 muzzleWorld;

    // Pose blends, eased in update() and read by draw(): 0..1 amounts of
    // "running", "off the ground" and "aiming the tool".
    float stride      = 0.0f;
    float air         = 0.0f;
    float aim         = 0.0f;
    float slidePose   = 0.0f;    // 0..1 into the slide's pose
    float backpedal   = 0.0f;    // 0..1 walking backwards (facing one way, going the other)
    float jumpGrace   = 0.0f;    // frames left in which a fresh jump can't be re-grounded
    // Water: how far in (0 = dry .. 1 = over the chest), how deep the chest
    // is under the surface (negative: that far above it; only meaningful
    // while wet > 0), the swimming pose blend and its stroke clock.
    float wet         = 0.0f;
    float chestDepth  = 0.0f;
    float swimPose    = 0.0f;
    float swimAnim    = 0.0f;
    float swimGo      = 0.0f;    // 0 treading water .. 1 swimming along
    bool  stroking    = false;   // swam up or down this frame

private:
    bool walkRequested = false;
};
