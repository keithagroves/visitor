#pragma once

#include "../math/vecmath.h"
#include "../graphics/renderer2d.h"
#include "../world/planet.h"

// A night crawler: the hostile creature that comes up out of the ground after
// dark (docs/gameplay.md, *Night*). It stalks the player along the ground,
// crouches when it is close, and pounces; a pounce that lands bites the suit.
// The light sword knocks it back, and a second cut kills it. When the sun
// reaches it, or there is nobody left to hunt, it digs itself back in.
//
// Simulated like the astronaut (gravity, the rock itself for collision, a
// step down to stay on falling ground) but kept PLANET-LOCAL: `local` is the
// feet relative to the planet's center, so it rides the orbit for free and
// keeps full float precision. Spawning, the bite and the sword are the
// caller's (main.cpp, "Night crawlers, cave bats and the light sword"); this class only knows how one moves
// and how it looks.
class Crawler {
public:
    enum class State { Emerging, Prowling, Crouching, Leaping, Recovering, Staggered, Dying, Burrowing, Gone };

    // Collision, at scale 1 (world units): the feet are the point itself, the
    // body a circle BODY_UP above them.
    static constexpr float FOOT_R    = 2.0f;
    static constexpr float BODY_R    = 7.0f;
    static constexpr float BODY_UP   = 8.0f;
    static constexpr float STEP_DOWN = 7.0f;
    static constexpr int   HP        = 2;       // sword hits to kill one

    // What it knows about the world this frame (all planet-local).
    struct Senses {
        Vec2  player;           // the player's feet (physical)
        bool  hunting = false;  // there is a player on foot here to go after
        Vec2  toSun;            // unit vector, center -> sun
        float speed = 2.0f;     // top prowling speed, world units / frame
    };

    // Comes up out of the ground with its feet at `local` (on the rock).
    void spawn(Vec2 local, unsigned int seed, float scale);

    // One step. Returns true while it is airborne in a pounce that has not
    // bitten yet (the caller tests the bite and then calls `bit()`).
    bool update(const Planet& p, const Senses& s, float dt, float timeScale);
    void bit() { bitten = true; }
    // A sword hit, pushing along `dir` (unit, planet-local). True if it died.
    bool hit(Vec2 dir);
    // Dig back in (dawn, or nobody to hunt).
    void retreat();

    bool alive() const { return state != State::Gone && state != State::Dying; }
    bool gone() const { return state == State::Gone; }
    // Can be hit by the sword: out of the ground and not already dying.
    bool hittable() const {
        return state != State::Gone && state != State::Dying && state != State::Burrowing &&
               !(state == State::Emerging && t < 0.4f);
    }
    // The middle of its body (planet-local, physical), for hits and bites.
    Vec2 bodyCenter() const;
    float radius() const { return BODY_R * scale; }

    // Drawn on the walking line (NearField::WALK_DROP below where it
    // physically is), like the astronaut. `body` / `shade` are the world's
    // tones for it, `eye` the glow of its eyes.
    void draw(Renderer2D& r, const Planet& p, const float body[3], const float shade[3], float time) const;

    Vec2  local, vel;
    State state = State::Gone;
    float t = 0.0f;             // seconds in the current state
    int   hp = HP;
    int   facing = 1;           // +1 = the way angles grow (screen right)
    float scale = 1.0f;
    float anim = 0.0f;          // gait clock
    float flash = 0.0f;         // seconds of the hit flash left
    float idle = 0.0f;          // seconds with nobody to hunt
    bool  grounded = false;
    bool  bitten = false;       // this pounce has already bitten
    int   swingHit = -1;        // the last sword swing that hit it (one hit per swing)
    unsigned int seed = 1;

private:
    void physics(const Planet& p, float timeScale, bool friction);
    float stuck = 0.0f;         // seconds pushing without getting anywhere
    float jumpGrace = 0.0f;
};
