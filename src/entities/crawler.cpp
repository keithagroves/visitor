#include "crawler.h"
#include "../world/nearfield.h"
#include "../world/outposts.h"
#include <algorithm>
#include <cmath>

// Night crawlers: how one moves (a small state machine over astronaut-style
// physics) and how it is drawn (flat vector shapes, like everything else that
// walks: docs/art-style.md — a body tone, a shade tone and one accent, the
// glow of the eyes, which is what you see of them in the dark).
namespace {

constexpr float EMERGE_TIME = 0.9f;     // seconds to come up out of the ground
constexpr float BURROW_TIME = 0.8f;     // ...and to go back in
constexpr float CROUCH_TIME = 0.42f;    // the wind-up before a pounce: the tell
constexpr float RECOVER_TIME = 0.55f;   // standing there after a pounce: the opening
constexpr float STAGGER_TIME = 0.45f;   // knocked back by the sword
constexpr float DIE_TIME = 0.55f;
constexpr float POUNCE_REACH = 80.0f;   // along the ground: close enough to crouch
constexpr float POUNCE_APEX = 20.0f;    // how high a pounce goes, world units
constexpr float HOP_HEIGHT = 30.0f;     // over a step it can't walk up
constexpr float LOSE_INTEREST = 5.0f;   // seconds with nobody to hunt before it digs in
constexpr float GIVE_UP = 1800.0f;      // along the ground: too far to bother
// The lantern at a trading post keeps them off: they won't come nearer to
// the stall than this, along the ground.
constexpr float POST_KEEPOUT = 320.0f;
constexpr float WALK_DAMP = 0.85f;

Vec2 upOf(Vec2 v) {
    float l = v.length();
    return l > 1.0f ? v * (1.0f / l) : Vec2(0.0f, -1.0f);
}

float easeOut(float k) { return 1.0f - (1.0f - k) * (1.0f - k); }

void capsule(Renderer2D& r, float ax, float ay, float bx, float by, float w) {
    r.line(ax, ay, bx, by, w);
    r.circle(ax, ay, w * 0.5f, 10);
    r.circle(bx, by, w * 0.5f, 10);
}

}  // namespace

void Crawler::spawn(Vec2 at, unsigned int s, float sc) {
    local = at;
    vel = Vec2(0, 0);
    state = State::Emerging;
    t = 0.0f;
    hp = HP;
    seed = s;
    scale = sc;
    anim = (s & 1023u) * 0.01f;
    flash = 0.0f;
    idle = 0.0f;
    grounded = true;
    bitten = false;
    swingHit = -1;
    stuck = 0.0f;
    jumpGrace = 0.0f;
    facing = 1;
}

Vec2 Crawler::bodyCenter() const { return local + upOf(local) * (BODY_UP * scale); }

void Crawler::retreat() {
    if (state == State::Burrowing || state == State::Dying || state == State::Gone) return;
    state = State::Burrowing;
    vel = Vec2(0, 0);
    t = 0.0f;
}

bool Crawler::hit(Vec2 dir) {
    if (!hittable()) return false;
    Vec2 up = upOf(local);
    hp -= 1;
    flash = 0.18f;
    vel = vel * 0.2f + dir * 3.2f + up * 1.6f;
    grounded = false;
    jumpGrace = 6.0f;
    t = 0.0f;
    if (hp <= 0) {
        state = State::Dying;
        return true;
    }
    state = State::Staggered;
    return false;
}

// Gravity, the rock, and friction — the astronaut's recipe (see
// Astronaut::update), in planet-local coordinates and with one body circle.
void Crawler::physics(const Planet& p, float timeScale, bool friction) {
    if (jumpGrace > 0.0f) jumpGrace -= timeScale;
    bool wasGrounded = grounded;

    float r = local.length();
    if (r < 1.0f) r = 1.0f;
    Vec2 down = local * (-1.0f / r);
    vel = vel + down * (p.gravity / (r * r) * timeScale);
    // In water it is slowed, like a wader.
    if (p.waterDepth(p.center + local + down * -2.0f) > 0.0f) vel = vel * powf(0.93f, timeScale);
    local = local + vel * timeScale;

    Vec2 up = upOf(local);
    bool contact = false;
    auto pushOut = [&](Vec2 offset, float rad, bool feet) {
        Vec2 probe = p.center + local + offset;
        float den = p.rockAt(probe);
        if (den <= -rad) return;
        Vec2 n = p.normalAt(probe);
        local = local + n * (den + rad);
        float into = vel.dot(n);
        if (into < 0.0f) vel = vel - n * into;
        if (feet && n.dot(up) > 0.45f) contact = true;
    };
    for (int pass = 0; pass < 2; pass++) {
        pushOut(Vec2(0, 0), FOOT_R, true);
        pushOut(up * (BODY_UP * scale), BODY_R * scale, false);
    }
    if (!contact && wasGrounded && jumpGrace <= 0.0f) {
        for (float k = 1.0f; k <= STEP_DOWN; k += 1.0f) {
            if (p.rockAt(p.center + local - up * k) > -FOOT_R) {
                local = local - up * (k - 1.0f);
                float outward = vel.dot(up);
                if (outward > 0.0f) vel = vel - up * outward;
                contact = true;
                break;
            }
        }
    }
    grounded = contact && jumpGrace <= 0.0f;
    if (grounded && friction) vel = vel * powf(WALK_DAMP, timeScale);
}

bool Crawler::update(const Planet& p, const Senses& s, float dt, float timeScale) {
    if (state == State::Gone) return false;
    t += dt;
    if (flash > 0.0f) flash -= dt;

    Vec2 up = upOf(local);
    Vec2 tangent(-up.y, up.x);              // the way angles grow
    float r = local.length();
    // Where the player is, along the ground (+ = the way angles grow) and
    // up from here.
    Vec2 pu = upOf(s.player);
    float along = atan2f(up.x * pu.y - up.y * pu.x, up.dot(pu)) * r;
    float rise = s.player.length() - r;
    int dir = along >= 0.0f ? 1 : -1;
    float sun = up.dot(s.toSun);

    // The gait runs on ground covered.
    float speedAlong = vel.dot(tangent);
    anim += (fabsf(speedAlong) * 0.11f + 0.02f) * timeScale;

    switch (state) {
    case State::Emerging:
        if (t >= EMERGE_TIME) { state = State::Prowling; t = 0.0f; }
        if (s.hunting) facing = dir;
        return false;
    case State::Burrowing:
        if (t >= BURROW_TIME) state = State::Gone;
        return false;
    case State::Dying:
        physics(p, timeScale, true);
        if (t >= DIE_TIME) state = State::Gone;
        return false;
    case State::Crouching:
        physics(p, timeScale, true);
        facing = dir;
        if (t >= CROUCH_TIME) {
            // A hop that peaks POUNCE_APEX up and comes down where the player
            // was, on any world's gravity.
            float g = p.gravity / (r * r);
            float vup = sqrtf(2.0f * g * POUNCE_APEX);
            float frames = 2.0f * vup / (g > 1e-6f ? g : 1e-6f);
            float horiz = fabsf(along) / frames;
            horiz = horiz < 0.9f ? 0.9f : (horiz > 3.4f ? 3.4f : horiz);
            vel = up * vup + tangent * (horiz * (float)dir);
            state = State::Leaping;
            t = 0.0f;
            bitten = false;
            grounded = false;
            jumpGrace = 6.0f;
        }
        return false;
    case State::Leaping:
        physics(p, timeScale, false);
        if (grounded && t > 0.15f) { state = State::Recovering; t = 0.0f; }
        return state == State::Leaping && !bitten;
    case State::Recovering:
        physics(p, timeScale, true);
        if (t >= RECOVER_TIME) { state = State::Prowling; t = 0.0f; }
        return false;
    case State::Staggered:
        physics(p, timeScale, true);
        if (t >= STAGGER_TIME && grounded) { state = State::Prowling; t = 0.0f; }
        return false;
    case State::Prowling:
    case State::Gone:
        break;
    }

    // ---- Prowling ---------------------------------------------------------
    if (sun > 0.02f) { retreat(); return false; }          // the light has reached it
    if (!s.hunting || fabsf(along) > GIVE_UP) {
        idle += dt;
        if (idle > LOSE_INTEREST) { retreat(); return false; }
        physics(p, timeScale, true);
        return false;
    }
    idle = 0.0f;
    facing = dir;

    if (grounded && fabsf(along) < POUNCE_REACH && rise > -30.0f && rise < 45.0f) {
        state = State::Crouching;
        t = 0.0f;
        physics(p, timeScale, true);
        return false;
    }

    // Hold back where going on would take it into a post's lantern light or
    // into the water.
    bool hold = false;
    {
        float toPost = 0.0f;
        if (Outposts::nearest(p, p.center + local, &toPost) >= 0) {
            float ahead = toPost * (float)dir;            // > 0: the post lies the way it is going
            if (ahead > 0.0f && fabsf(toPost) < POST_KEEPOUT + 20.0f) hold = true;
            // Caught inside it (the player led it there): back out.
            if (fabsf(toPost) < POST_KEEPOUT - 10.0f) { dir = toPost > 0.0f ? -1 : 1; hold = false; }
        }
        if (p.waterDepth(p.center + local + tangent * (16.0f * (float)dir) + up * 2.0f) > 4.0f) hold = true;
    }

    if (!hold) {
        if (grounded) {
            float push = s.speed * (1.0f - powf(WALK_DAMP, timeScale));
            vel = vel + tangent * (push * (float)dir);
            // Pushing and not getting anywhere: a step too high to walk up. Hop.
            if (speedAlong * (float)dir < s.speed * 0.25f) stuck += dt;
            else stuck = 0.0f;
            if (stuck > 0.35f) {
                float g = p.gravity / (r * r);
                vel = vel + up * sqrtf(2.0f * g * HOP_HEIGHT) + tangent * (0.8f * (float)dir);
                grounded = false;
                jumpGrace = 6.0f;
                stuck = 0.0f;
            }
        } else {
            float room = s.speed * 0.8f - speedAlong * (float)dir;
            if (room > 0.0f) vel = vel + tangent * (std::min(room, 0.08f * timeScale) * (float)dir);
        }
    } else {
        stuck = 0.0f;
    }
    physics(p, timeScale, true);
    return false;
}

void Crawler::draw(Renderer2D& r, const Planet& p, const float body[3], const float shade[3], float time) const {
    if (state == State::Gone) return;
    Vec2 world = p.center + local;
    float standAngle = atan2d(local.y, local.x) + 90.0f;

    // Pose, from the state.
    float grow = 1.0f;          // coming up out of / going back into the ground
    float alpha = 1.0f;
    float crouch = 0.0f;        // 0..1 low and coiled
    float stretch = 0.0f;       // 0..1 stretched out in the air
    float tilt = 0.0f;          // degrees, nose up +
    float eyes = 0.75f;         // eye glow
    float jaw = 0.0f;           // 0..1 mouth open
    switch (state) {
    case State::Emerging:   grow = easeOut(t / EMERGE_TIME); break;
    case State::Burrowing:  grow = 1.0f - t / BURROW_TIME; eyes = 0.5f; break;
    case State::Crouching:  crouch = t / CROUCH_TIME; eyes = 0.75f + 0.25f * crouch; jaw = crouch * 0.6f; break;
    case State::Leaping: {
        stretch = 1.0f;
        jaw = 1.0f;
        eyes = 1.0f;
        Vec2 up = local * (1.0f / std::max(1.0f, local.length()));
        Vec2 tan(-up.y, up.x);
        float vu = vel.dot(up), va = vel.dot(tan) * (float)facing;
        tilt = atan2d(vu, va > 0.3f ? va : 0.3f) * 0.6f;
        break;
    }
    case State::Recovering: crouch = 0.3f * (1.0f - t / RECOVER_TIME); break;
    case State::Staggered:  tilt = 22.0f * (1.0f - t / STAGGER_TIME); eyes = 0.45f; jaw = 0.4f; break;
    case State::Dying:      alpha = 1.0f - t / DIE_TIME; tilt = 70.0f * (t / DIE_TIME); eyes = 0.3f * alpha; break;
    default: break;
    }
    if (grow < 0.0f) grow = 0.0f;
    if (alpha < 0.0f) alpha = 0.0f;
    // Seeded variety within a world's kind: how many spines, how long the tail.
    int spines = 2 + (int)(seed % 3u);
    float tail = 0.8f + 0.1f * (float)((seed >> 3) % 5u);

    r.pushMatrix();
    r.translate(world.x, world.y);
    r.rotate(standAngle);
    // Drawn on the walking line, NearField::WALK_DROP below where it stands.
    r.translate(0.0f, NearField::WALK_DROP);
    // Contact shadow (on the ground, so before it grows or tilts).
    if (grounded || state == State::Emerging || state == State::Burrowing) {
        r.fill(0.02f, 0.03f, 0.05f, 0.25f * alpha * grow);
        r.ellipse(0.0f, 0.5f, 13.0f * scale, 3.6f * scale, 16);
    }
    r.scale(scale * (float)facing, scale);
    // Grows up out of the ground from its feet (y = 0 here, +y down).
    r.scale(1.0f, grow);
    r.rotate(-tilt);
    r.scale(1.0f + 0.15f * stretch, 1.0f - 0.12f * stretch - 0.15f * crouch);
    r.translate(0.0f, 2.5f * crouch);

    // ---- Legs: far pair in shade, near pair over the body ------------------
    // Two pairs, front and back, stepping in turn; tucked back in a pounce.
    auto leg = [&](float hipX, float phase, bool near) {
        float s = sinf(anim + phase), c = cosf(anim + phase);
        float footX = hipX + s * 4.0f * (1.0f - stretch) + (hipX > 0 ? 6.0f : -6.0f) * stretch;
        float lift = (c > 0.0f ? c * 2.5f : 0.0f) * (1.0f - stretch) + 4.0f * stretch;
        float kneeX = (hipX + footX) * 0.5f + (hipX > 0 ? 2.5f : -2.5f);
        const float* col = near ? body : shade;
        r.fill(col[0], col[1], col[2], alpha);
        capsule(r, hipX, -4.0f, kneeX, -2.5f - lift * 0.5f, 3.2f);
        capsule(r, kneeX, -2.5f - lift * 0.5f, footX, -lift, 2.6f);
    };
    leg(6.0f, 3.14159f, false);
    leg(-7.0f, 0.0f, false);

    // Tail, swaying.
    float sway = sinf(time * 1.7f + (float)(seed % 97u)) * 2.0f;
    r.fill(shade[0], shade[1], shade[2], alpha);
    r.line(-11.0f, -7.0f, -11.0f - 9.0f * tail, -11.0f + sway - 3.0f * stretch, 3.0f);
    r.circle(-11.0f - 9.0f * tail, -11.0f + sway - 3.0f * stretch, 1.5f, 8);

    // Body: a low, hunched back, a lighter belly, spines along the ridge.
    r.fill(body[0], body[1], body[2], alpha);
    r.ellipse(-1.0f, -8.0f, 13.0f, 6.0f, 22);
    r.ellipse(-3.0f, -11.0f, 9.0f, 5.5f, 18);
    r.fill(shade[0], shade[1], shade[2], alpha);
    r.ellipse(0.0f, -4.6f, 10.0f, 2.4f, 16);
    for (int i = 0; i < spines; i++) {
        float x = -9.0f + 12.0f * ((float)i + 0.5f) / (float)spines;
        float y = -14.5f + fabsf(x + 3.0f) * 0.18f;
        r.triangle(x - 2.2f, y + 1.5f, x + 2.2f, y + 1.5f, x - 0.8f, y - 4.0f);
    }

    // Head, out in front and a little low; the jaw drops open to bite.
    r.fill(body[0], body[1], body[2], alpha);
    r.ellipse(11.0f, -8.5f, 6.0f, 4.6f, 18);
    if (jaw > 0.01f) {
        r.fill(0.08f, 0.04f, 0.06f, alpha);
        r.triangle(10.0f, -7.0f, 17.5f, -7.5f, 16.5f, -7.0f + 4.0f * jaw);
        r.fill(0.92f, 0.88f, 0.80f, alpha);
        r.triangle(13.0f, -7.3f, 14.2f, -7.4f, 13.6f, -5.6f);
        r.triangle(15.2f, -7.4f, 16.4f, -7.5f, 15.8f, -5.8f);
    }
    r.fill(body[0], body[1], body[2], alpha);
    capsule(r, 7.0f, -5.5f, 13.0f, -5.5f + 3.5f * jaw, 2.6f);

    // Near legs.
    leg(8.0f, 0.0f, true);
    leg(-5.0f, 3.14159f, true);

    // Hit flash: the same body, lit up for a moment.
    if (flash > 0.0f) {
        float f = flash / 0.18f;
        r.fill(1.0f, 0.95f, 0.9f, 0.75f * f * alpha);
        r.ellipse(-1.0f, -8.0f, 13.0f, 6.0f, 22);
        r.ellipse(11.0f, -8.5f, 6.0f, 4.6f, 18);
    }

    // Eyes: the one accent, emitted light — a small hot core and one smooth
    // halo, so in the dark that is what you see first.
    static const float EYE[3] = {1.0f, 0.42f, 0.18f};
    r.setBlend(Renderer2D::Blend::Additive);
    r.fill(EYE[0], EYE[1], EYE[2], 0.16f * eyes * alpha);
    r.ellipse(13.2f, -10.1f, 3.6f, 2.6f, 16);
    r.fill(EYE[0], EYE[1] + 0.15f, EYE[2] + 0.1f, 0.95f * eyes * alpha);
    r.circle(14.4f, -10.2f, 1.2f, 8);
    r.circle(12.0f, -10.0f, 1.0f, 8);
    r.setBlend(Renderer2D::Blend::Normal);

    r.popMatrix();
}
