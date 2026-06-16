#pragma once

#include "../math/vecmath.h"
#include "../graphics/renderer2d.h"
#include "../world/planet.h"

class Astronaut {
public:
    static constexpr float RADIUS           = 12.0f;
    static constexpr float WALK_FORCE       = 3.0f;
    static constexpr float WALK_DAMP        = 0.85f;
    static constexpr float JUMP_SPEED       = 8.0f;
    static constexpr float SCALE            = 0.5f;
    static constexpr float JETPACK_MAX_FUEL = 100.0f;
    static constexpr float JETPACK_THRUST   = 0.25f;
    static constexpr float JETPACK_DRAIN    = 0.55f;
    static constexpr float JETPACK_CHARGE   = 0.55f;

    void spawn(Vec2 position);

    void update(const Planet& planet, float timeScale);
    void walkLeft(const Planet& planet, float timeScale);
    void walkRight(const Planet& planet, float timeScale);
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

private:
    bool walkRequested = false;
};
