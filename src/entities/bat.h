#pragma once

#include "../math/vecmath.h"
#include "../graphics/renderer2d.h"
#include "../world/planet.h"

// A cave bat: the hostile flyer of the natural caves, day or night
// (docs/gameplay.md, *Night* and *Caves*). It hangs from the ceiling beside a
// stalactite until someone comes down into its cave, then drops, flutters
// round to hover above and to one side of them, holds still for a moment
// (the tell: wings high, eyes flaring) and dives straight at where they
// were. A dive that connects bites the suit; it pulls up and comes round
// again. One cut of the light sword kills it.
//
// It flies — no gravity while it lives — and bounces off the rock itself
// (Planet::rockAt). Planet-local, like the crawler. It never leaves the
// ground: it turns back short of the surface. Spawning, the bite and the
// sword are the caller's (main.cpp, "Night crawlers, cave bats and the light sword").
class Bat {
public:
    enum class State { Roosting, Waking, Flying, Windup, Diving, Retreating, Staggered, Dying, Leaving, Gone };

    static constexpr float RADIUS = 7.0f;       // its body, for the rock, hits and bites
    static constexpr int   HP     = 1;          // sword hits to kill one

    struct Senses {
        Vec2 player;             // the player's chest (physical, planet-local)
        bool hunting = false;    // someone on foot down here to go after
    };

    // Hanging at `roost` (planet-local, its body's center) under the ceiling.
    void spawn(Vec2 roost, unsigned int seed, int roostId);

    // One step. Returns true while it is diving and has not bitten yet (the
    // caller tests the bite and then calls `bit()`).
    bool update(const Planet& p, const Senses& s, float dt, float timeScale);
    void bit() { bitten = true; }
    // A sword hit, pushing along `dir` (unit, planet-local). True if it died.
    bool hit(Vec2 dir);
    // Fly off and fade (nobody left down here).
    void leave();

    bool alive() const { return state != State::Gone && state != State::Dying && state != State::Leaving; }
    bool gone() const { return state == State::Gone; }
    bool awake() const { return alive() && state != State::Roosting; }
    bool hittable() const { return alive(); }
    float radius() const { return RADIUS; }

    // Drawn on the walking line (NearField::WALK_DROP below where it is),
    // `fade` times its own alpha (the caller hides the caves' life from the
    // surface).
    void draw(Renderer2D& r, const Planet& p, const float body[3], const float shade[3], float time,
              float fade) const;

    Vec2  local, vel;
    Vec2  roost;
    State state = State::Gone;
    float t = 0.0f;             // seconds in the current state
    int   hp = HP;
    int   facing = 1;
    float flap = 0.0f;          // wingbeat clock
    float flash = 0.0f;
    float idle = 0.0f;          // seconds with nobody to hunt
    float hoverFor = 1.5f;      // seconds of circling before the next dive
    bool  bitten = false;
    int   swingHit = -1;        // the last sword swing that hit it
    int   roostId = -1;         // which roost it came from (the caller's bookkeeping)
    unsigned int seed = 1;

private:
    void fly(const Planet& p, float timeScale);   // move, bounce off the rock, stay underground
    bool bumped = false;        // hit the rock this step
    Vec2 diveDir;
};
