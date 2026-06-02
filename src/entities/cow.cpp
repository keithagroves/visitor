#include "cow.h"
#include "lander.h"
#include <cmath>

void Cow::recomputeOrigin(const Planet& planet) {
    origin_ = Vec2(planet.center.x + surfaceRadius_ * cosd(surfaceAngle_),
                   planet.center.y + surfaceRadius_ * sind(surfaceAngle_));
}

void Cow::spawn(const Planet& planet, float surfaceAngle, float surfaceRadius) {
    surfaceAngle_ = surfaceAngle;
    surfaceRadius_ = surfaceRadius;
    state = State::OnGround;
    vel = Vec2(0, 0);
    recomputeOrigin(planet);
    pos = origin_;
}

void Cow::abduct() {
    if (state == State::OnGround || state == State::Falling) state = State::Carried;
}

void Cow::drop() {
    if (state == State::Carried) state = State::Falling;
}

bool Cow::update(const Planet& planet, const Lander& lander, int cargo,
                 int maxCargo, float timeScale) {
    if (state == State::Stowed) return false;

    recomputeOrigin(planet);

    if (state == State::Carried) {
        // Kinematic lift straight at the ship at a mass-capped speed.
        float dx = lander.pos.x - pos.x;
        float dy = lander.pos.y - pos.y;
        float d = sqrtf(dx * dx + dy * dy);
        float step = LIFT_SPEED * timeScale;
        if (d <= step) {
            pos = lander.pos;
            vel = Vec2(0, 0);
        } else {
            float inv = 1.0f / d;
            pos.x += dx * inv * step;
            pos.y += dy * inv * step;
            vel = Vec2(dx * inv * LIFT_SPEED, dy * inv * LIFT_SPEED);
        }
        if (cargo < maxCargo && dist(pos.x, pos.y, lander.pos.x, lander.pos.y) < 25.0f) {
            state = State::Stowed;
            return true;   // caller bumps cargo + score
        }
        return false;
    }

    if (state == State::Falling) {
        // Gravity from the home planet pulls it back down; lift velocity carries
        // over so a tug-then-release reads as a toss.
        float gx = planet.center.x - pos.x;
        float gy = planet.center.y - pos.y;
        float distSq = gx * gx + gy * gy;
        if (distSq < 1) distSq = 1;
        float distance = sqrtf(distSq);
        float force = planet.gravity / distSq;
        vel.x += (gx / distance) * force * timeScale;
        vel.y += (gy / distance) * force * timeScale;
        pos.x += vel.x * timeScale;
        pos.y += vel.y * timeScale;

        float landAngle = atan2d(pos.y - planet.center.y, pos.x - planet.center.x);
        float surfaceR = planet.surfaceRadius(landAngle);
        if (distance <= surfaceR) {
            surfaceAngle_ = landAngle;
            surfaceRadius_ = surfaceR;
            recomputeOrigin(planet);
            pos = origin_;
            vel = Vec2(0, 0);
            state = State::OnGround;
        }
        return false;
    }

    // OnGround: spring back toward the origin point with heavy damping.
    float dx = origin_.x - pos.x;
    float dy = origin_.y - pos.y;
    vel.x += dx * PULL_STRENGTH * 2.0f * timeScale;
    vel.y += dy * PULL_STRENGTH * 2.0f * timeScale;
    vel *= 0.85f;
    pos += vel * timeScale;
    return false;
}

void Cow::draw(Renderer2D& r, const Planet& planet) const {
    if (state == State::Stowed) return;

    // Stand upright relative to the planet: local +Y points to the planet
    // center (legs down), matching cow.js's standAngle = atan2(dy,dx)+90.
    float standAngle = atan2d(pos.y - planet.center.y, pos.x - planet.center.x) + 90.0f;

    r.pushMatrix();
    r.translate(pos.x, pos.y);
    r.rotate(standAngle);
    r.scale(SCALE);

    // Simple side-on cow in local space: head points +X, legs hang at +Y.
    // Legs.
    r.fill(0.15f, 0.13f, 0.12f, 1.0f);
    r.quad(-16, 8, -11, 8, -11, 20, -16, 20);
    r.quad(8, 8, 13, 8, 13, 20, 8, 20);

    // Body (warm off-white).
    r.fill(0.85f, 0.82f, 0.78f, 1.0f);
    r.ellipse(-1, 0, 21, 12, 22);

    // Dark patches.
    r.fill(0.18f, 0.15f, 0.14f, 1.0f);
    r.ellipse(-9, -2, 6, 5, 14);
    r.ellipse(6, 3, 5, 4, 14);

    // Head + snout.
    r.fill(0.80f, 0.77f, 0.73f, 1.0f);
    r.ellipse(18, -3, 7, 6, 16);
    r.fill(0.55f, 0.45f, 0.45f, 1.0f);
    r.ellipse(23, -1, 3.5f, 3.0f, 12);
    // Ear + eye.
    r.fill(0.18f, 0.15f, 0.14f, 1.0f);
    r.ellipse(14, -8, 3, 2, 10);
    r.fill(0.05f, 0.05f, 0.05f, 1.0f);
    r.circle(20, -4, 1.2f, 8);

    r.popMatrix();
}
