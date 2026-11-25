#include "bat.h"
#include "../world/nearfield.h"
#include <algorithm>
#include <cmath>

// Cave bats: how one flies (a small state machine, no gravity, bouncing off
// the rock) and how it is drawn — flat shapes, seen from the front: a dark
// round body, wings that beat, and a pale mask with dark eye holes, the one
// thing that reads in an unlit cave. The eyes light up ember only for the
// tell before a dive.
namespace {

constexpr float WAKE_RANGE = 210.0f;    // someone this close (and underground) wakes it
constexpr float WAKE_TIME = 0.45f;      // dropping off the ceiling, wings opening
constexpr float WINDUP_TIME = 0.38f;    // holding still before a dive: the tell
constexpr float DIVE_SPEED = 5.2f;      // world units / frame
constexpr float DIVE_TIME = 0.6f;
constexpr float RETREAT_TIME = 0.7f;
constexpr float STAGGER_TIME = 0.3f;
constexpr float DIE_TIME = 0.7f;
constexpr float LEAVE_TIME = 0.9f;
constexpr float HOVER_SPEED = 2.4f;
constexpr float DIVE_FROM = 190.0f;     // only dives from this close
constexpr float LOSE_INTEREST = 3.0f;
constexpr float CEILING_GAP = 14.0f;    // keeps this far under the ground's outline

Vec2 upOf(Vec2 v) {
    float l = v.length();
    return l > 1.0f ? v * (1.0f / l) : Vec2(0.0f, -1.0f);
}

}  // namespace

void Bat::spawn(Vec2 at, unsigned int s, int id) {
    local = roost = at;
    vel = Vec2(0, 0);
    state = State::Roosting;
    t = 0.0f;
    hp = HP;
    facing = (s & 1u) ? 1 : -1;
    flap = (s & 255u) * 0.1f;
    flash = 0.0f;
    idle = 0.0f;
    hoverFor = 1.2f + (float)((s >> 8) % 100u) * 0.01f;
    bitten = false;
    swingHit = -1;
    roostId = id;
    seed = s;
    bumped = false;
}

void Bat::leave() {
    if (!alive()) return;
    state = State::Leaving;
    t = 0.0f;
}

bool Bat::hit(Vec2 dir) {
    if (!hittable()) return false;
    hp -= 1;
    flash = 0.15f;
    vel = dir * 4.0f;
    t = 0.0f;
    if (hp <= 0) {
        state = State::Dying;
        return true;
    }
    state = State::Staggered;
    return false;
}

void Bat::fly(const Planet& p, float timeScale) {
    bumped = false;
    local = local + vel * timeScale;
    Vec2 probe = p.center + local;
    float den = p.rockAt(probe);
    if (den > -RADIUS) {
        Vec2 n = p.normalAt(probe);
        local = local + n * (den + RADIUS);
        float vn = vel.dot(n);
        if (vn < 0.0f) vel = vel - n * (vn * 1.6f);     // bounce, losing a little
        bumped = true;
    }
    // Never up into the open air: turn back short of the ground's outline.
    Vec2 up = upOf(local);
    float top = p.surfaceRadius(atan2d(local.y, local.x)) - CEILING_GAP;
    if (state != State::Dying && local.length() > top) {
        local = up * top;
        float out = vel.dot(up);
        if (out > 0.0f) vel = vel - up * out;
        bumped = true;
    }
}

bool Bat::update(const Planet& p, const Senses& s, float dt, float timeScale) {
    if (state == State::Gone) return false;
    t += dt;
    if (flash > 0.0f) flash -= dt;
    if (state != State::Roosting && state != State::Diving) flap += 0.45f * timeScale;

    Vec2 up = upOf(local);
    Vec2 tangent(-up.y, up.x);
    Vec2 to = s.player - local;
    float d = to.length();
    auto steer = [&](Vec2 goal, float speed, float rate) {
        Vec2 want = (goal - local) * 0.04f;
        float l = want.length();
        if (l > speed) want = want * (speed / l);
        float k = std::min(1.0f, rate * timeScale);
        vel = vel + (want - vel) * k;
    };

    switch (state) {
    case State::Roosting:
        vel = Vec2(0, 0);
        if (s.hunting && d < WAKE_RANGE) { state = State::Waking; t = 0.0f; }
        return false;
    case State::Waking:
        vel = up * -0.7f;
        fly(p, timeScale);
        if (t >= WAKE_TIME) { state = State::Flying; t = 0.0f; }
        return false;
    case State::Windup:
        vel = vel * powf(0.8f, timeScale);
        fly(p, timeScale);
        if (to.dot(tangent) != 0.0f) facing = to.dot(tangent) > 0.0f ? 1 : -1;
        if (t >= WINDUP_TIME) {
            diveDir = d > 1.0f ? to * (1.0f / d) : up * -1.0f;
            state = State::Diving;
            t = 0.0f;
            bitten = false;
        }
        return false;
    case State::Diving:
        vel = diveDir * DIVE_SPEED;
        fly(p, timeScale);
        if (bumped || bitten || t >= DIVE_TIME) { state = State::Retreating; t = 0.0f; return false; }
        return true;
    case State::Retreating:
        steer(local + up * 80.0f - diveDir * 40.0f, HOVER_SPEED * 1.2f, 0.10f);
        fly(p, timeScale);
        if (t >= RETREAT_TIME) {
            state = State::Flying;
            t = 0.0f;
            hoverFor = 1.0f + (float)((seed + (unsigned int)(flap * 7.0f)) % 120u) * 0.01f;
        }
        return false;
    case State::Staggered:
        vel = vel * powf(0.9f, timeScale);
        fly(p, timeScale);
        if (t >= STAGGER_TIME) { state = State::Flying; t = 0.0f; }
        return false;
    case State::Dying:
        vel = vel + up * (-0.12f * timeScale);
        fly(p, timeScale);
        if (t >= DIE_TIME) state = State::Gone;
        return false;
    case State::Leaving:
        steer(roost, HOVER_SPEED, 0.08f);
        fly(p, timeScale);
        if (t >= LEAVE_TIME) state = State::Gone;
        return false;
    case State::Flying:
    case State::Gone:
        break;
    }

    // ---- Flying: hover above and to one side of the player, bobbing -------
    if (!s.hunting) {
        idle += dt;
        if (idle > LOSE_INTEREST) { leave(); return false; }
        steer(roost, HOVER_SPEED, 0.06f);
        fly(p, timeScale);
        return false;
    }
    idle = 0.0f;
    float side = (local - s.player).dot(tangent) >= 0.0f ? 1.0f : -1.0f;
    float bob = sinf(t * 2.3f + (float)(seed % 50u)) * 10.0f;
    steer(s.player + up * (60.0f + bob) + tangent * (70.0f * side), HOVER_SPEED, 0.07f);
    fly(p, timeScale);
    facing = to.dot(tangent) >= 0.0f ? 1 : -1;
    if (t > hoverFor && d < DIVE_FROM) { state = State::Windup; t = 0.0f; }
    return false;
}

void Bat::draw(Renderer2D& r, const Planet& p, const float body[3], const float shade[3], float time,
               float fade) const {
    if (state == State::Gone || fade <= 0.001f) return;
    float alpha = fade;
    if (state == State::Dying) alpha *= std::max(0.0f, 1.0f - t / DIE_TIME);
    if (state == State::Leaving) alpha *= std::max(0.0f, 1.0f - t / LEAVE_TIME);
    Vec2 up = upOf(local);
    Vec2 at = p.center + local - up * NearField::WALK_DROP;
    float standAngle = atan2d(local.y, local.x) + 90.0f;

    // Wing beat: -1 down .. +1 up. Held high for the tell, swept back in a
    // dive, folded round the body at rest.
    float w = sinf(flap);
    if (state == State::Windup) w = 1.0f;
    if (state == State::Diving) w = -0.8f;
    bool folded = state == State::Roosting;
    float eyes = state == State::Windup ? std::min(1.0f, t / (WINDUP_TIME * 0.6f)) : (state == State::Diving ? 1.0f : 0.0f);

    r.pushMatrix();
    r.translate(at.x, at.y);
    r.rotate(standAngle);
    if (folded) r.rotate(180.0f);                         // hangs upside down
    if (state == State::Dying) r.rotate(t * 420.0f);       // tumbles
    if (state == State::Diving) r.rotate(atan2d(vel.dot(Vec2(-up.y, up.x)), -vel.dot(up)) * -0.3f);

    if (folded) {
        // Wrapped in its wings, a little sway.
        float sway = sinf(time * 0.9f + (float)(seed % 40u)) * 4.0f;
        r.rotate(sway);
        r.fill(shade[0], shade[1], shade[2], alpha);
        r.ellipse(0.0f, 1.0f, 6.0f, 9.5f, 18);
        r.triangle(-2.0f, -8.0f, 2.0f, -8.0f, 0.0f, -11.5f);   // the feet, hooked on the rock
    } else {
        // Wings: a scalloped membrane fanned from each shoulder.
        auto wing = [&](float sx) {
            r.pushMatrix();
            r.scale(sx, 1.0f);
            r.translate(4.0f, -2.0f);
            r.rotate(-38.0f * w);
            r.fill(shade[0], shade[1], shade[2], alpha);
            r.triangle(0, 0, 17, -3, 14, 4);
            r.triangle(0, 0, 14, 4, 9, 2.5f);
            r.triangle(0, 0, 9, 2.5f, 5.5f, 6);
            r.triangle(0, 0, 5.5f, 6, 0, 4);
            r.fill(body[0], body[1], body[2], alpha);
            r.line(0, 0, 17, -3, 1.6f);                        // the arm along the leading edge
            r.popMatrix();
        };
        wing(1.0f);
        wing(-1.0f);
        r.fill(body[0], body[1], body[2], alpha);
        r.ellipse(0.0f, 0.0f, 5.5f, 6.5f, 18);
        r.triangle(-4.0f, -4.0f, -1.2f, -5.5f, -4.2f, -10.5f); // ears
        r.triangle(4.0f, -4.0f, 1.2f, -5.5f, 4.2f, -10.5f);
    }

    // The mask: a pale face plate with two dark eye holes — what you see of
    // it in the dark. The eyes catch fire for the tell.
    float mx = folded ? 0.0f : 1.0f * (float)facing;
    float my = folded ? 3.5f : -1.5f;
    r.fill(0.86f, 0.84f, 0.79f, alpha);
    r.ellipse(mx, my, 3.9f, 3.3f, 14);
    if (folded) {
        r.fill(0.05f, 0.04f, 0.06f, alpha);
        r.line(mx - 2.6f, my - 0.6f, mx - 0.8f, my - 0.6f, 0.8f);   // asleep: eyes shut
        r.line(mx + 0.8f, my - 0.6f, mx + 2.6f, my - 0.6f, 0.8f);
    } else {
        r.fill(0.05f, 0.04f, 0.06f, alpha);
        r.ellipse(mx - 1.6f, my - 0.3f, 1.1f, 1.5f, 8);
        r.ellipse(mx + 1.6f, my - 0.3f, 1.1f, 1.5f, 8);
        if (eyes > 0.0f) {
            r.setBlend(Renderer2D::Blend::Additive);
            r.fill(1.0f, 0.45f, 0.18f, 0.9f * eyes * alpha);
            r.circle(mx - 1.6f, my - 0.3f, 0.9f, 8);
            r.circle(mx + 1.6f, my - 0.3f, 0.9f, 8);
            r.fill(1.0f, 0.42f, 0.18f, 0.16f * eyes * alpha);
            r.ellipse(mx, my - 0.3f, 4.5f, 2.6f, 12);
            r.setBlend(Renderer2D::Blend::Normal);
        }
    }

    if (flash > 0.0f) {
        r.fill(1.0f, 0.95f, 0.9f, 0.8f * (flash / 0.15f) * alpha);
        r.ellipse(0.0f, 0.0f, 6.0f, 7.0f, 16);
    }
    r.popMatrix();
}
