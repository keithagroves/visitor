#pragma once

#include "../math/vecmath.h"
#include "../graphics/renderer2d.h"
#include "../world/planet.h"

// An abductable cow, ported from cow-abductor's cow.js. Lives on the planet
// surface and runs a small state machine: it idles on the ground (wobbling back
// to its origin), gets tractor-lifted toward the ship when caught in the beam,
// falls back under gravity when released — or when the ground under it is cut
// away — and is "stowed" once it reaches the hatch. It lands on whatever rock
// is below it, so it will drop to the floor of a hole. The lander is
// forward-declared to avoid a circular include.
class Lander;

class Cow {
public:
    enum class State { OnGround, Carried, Falling, Stowed };

    static constexpr float PULL_STRENGTH = 0.15f;
    static constexpr float LIFT_SPEED = 1.4f;   // heavy: capped reel-in speed
    static constexpr float SIZE = 50.0f;
    // A standing cow's center is this far above the ground it stands on (its
    // hooves are then drawn on the walking line, a little below the outline).
    static constexpr float STAND_UP = 7.0f;
    static constexpr float SCALE = 0.8f;

    void spawn(const Planet& planet, float surfaceAngle, float surfaceRadius);

    // Advance one step. Returns true on the frame it stows (so the caller can
    // bump cargo/score). `cargo`/`maxCargo` gate whether it can latch in.
    bool update(const Planet& planet, const Lander& lander, int cargo,
                int maxCargo, float timeScale);

    void draw(Renderer2D& r, const Planet& planet) const;

    void abduct();  // beam caught it -> Carried
    void drop();    // beam lost it  -> Falling

    State state = State::OnGround;
    Vec2 pos;
    Vec2 vel;

private:
    void recomputeOrigin(const Planet& planet);

    float surfaceAngle_ = 0;
    float surfaceRadius_ = 0;
    Vec2 origin_;
};
