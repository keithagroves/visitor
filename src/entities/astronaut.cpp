#include "astronaut.h"
#include <cmath>

// The astronaut is drawn as flat vector shapes through Renderer2D, like the
// ship and the cows — not a pixel sprite — so it stays crisp at any zoom and
// sits in the same art style: a handful of flat tones, no outlines, no
// gradients. The figure is a small jointed puppet (two-segment legs and arms,
// torso, backpack, helmet) posed every frame from the simulation state.
namespace {

struct Rgb { float r, g, b; };
// Limited palette: off-white suit with one shade tone, dark boots/gloves/visor
// for contrast against pale ground and sky, and a single warm accent so the
// figure is never mistaken for a cow or a snow patch at a distance. The visor
// carries the same teal as the ship's cockpit glow.
const Rgb SUIT      {0.92f, 0.93f, 0.95f};
const Rgb SUIT_SHADE{0.70f, 0.74f, 0.82f};
const Rgb DARK      {0.15f, 0.18f, 0.27f};
const Rgb PACK      {0.44f, 0.48f, 0.58f};
const Rgb PACK_DARK {0.30f, 0.34f, 0.44f};
const Rgb ACCENT    {0.96f, 0.56f, 0.20f};
const Rgb VISOR     {0.06f, 0.09f, 0.17f};
const Rgb TEAL      {0.30f, 0.90f, 0.84f};

// Body plan, in local units (drawn at Astronaut::SCALE, so 2 units = 1 world
// unit). +x is the way the figure faces, +y is down toward the planet.
const float HIP_Y   = 10.0f;    // hip joint
const float THIGH   = 9.0f, SHIN = 9.0f;
const float GROUND  = HIP_Y + THIGH + SHIN;   // ankle height when standing straight
const float UPPER   = 8.0f, FORE = 7.5f;      // arm segments
const float LEG_W   = 7.6f, ARM_W = 6.2f;

// Joint angles in degrees, measured from straight down, positive = forward.
struct Pose {
    float thighN, shinN, thighF, shinF;   // near / far leg
    float armN, foreN, armF, foreF;       // near / far arm
    float lean;                           // upper body tilt (forward +)
};

Pose mix(const Pose& a, const Pose& b, float t) {
    auto m = [t](float x, float y) { return x + (y - x) * t; };
    return Pose{m(a.thighN, b.thighN), m(a.shinN, b.shinN), m(a.thighF, b.thighF), m(a.shinF, b.shinF),
                m(a.armN, b.armN), m(a.foreN, b.foreN), m(a.armF, b.armF), m(a.foreF, b.foreF),
                m(a.lean, b.lean)};
}

Vec2 dirDown(float deg) { return Vec2(sind(deg), cosd(deg)); }

void fillc(Renderer2D& r, const Rgb& c, float a = 1.0f) { r.fill(c.r, c.g, c.b, a); }

// A limb segment with round ends.
void capsule(Renderer2D& r, Vec2 a, Vec2 b, float w) {
    r.line(a.x, a.y, b.x, b.y, w);
    r.circle(a.x, a.y, w * 0.5f, 14);
    r.circle(b.x, b.y, w * 0.5f, 14);
}

// Axis-aligned box with round corners.
void roundBox(Renderer2D& r, float x0, float y0, float x1, float y1, float rad) {
    r.quad(x0 + rad, y0, x1 - rad, y0, x1 - rad, y1, x0 + rad, y1);
    r.quad(x0, y0 + rad, x1, y0 + rad, x1, y1 - rad, x0, y1 - rad);
    r.circle(x0 + rad, y0 + rad, rad, 12);
    r.circle(x1 - rad, y0 + rad, rad, 12);
    r.circle(x1 - rad, y1 - rad, rad, 12);
    r.circle(x0 + rad, y1 - rad, rad, 12);
}

}  // namespace

void Astronaut::spawn(Vec2 position) {
    pos           = position;
    vel           = Vec2(0, 0);
    walkAnim      = 0.0f;
    idleAnim      = 0.0f;
    facing        = 1;
    grounded      = false;
    walking       = false;
    jetting       = false;
    walkRequested = false;
    jetpackFuel   = JETPACK_MAX_FUEL;
    active        = true;
    aiming        = false;
    stride        = 0.0f;
    air           = 0.0f;
    aim           = 0.0f;
    jumpGrace     = 0.0f;
    muzzleWorld   = position;
}

void Astronaut::update(const Planet& planet, float timeScale) {
    if (!active) return;

    idleAnim += timeScale * 0.08f;
    if (jumpGrace > 0.0f) jumpGrace -= timeScale;

    // Standing still on the ground with nothing asked of us: remember exactly
    // where, so gravity nudging us into a slope and the collision nudging us
    // back out can't add up to a slow creep downhill.
    bool wasGrounded = grounded;
    bool resting = wasGrounded && !walkRequested && !jetting && jumpGrace <= 0.0f;
    Vec2 restPos = pos;

    // 1/r² gravity toward planet center (same formula as the cow's Falling state).
    float dx = planet.center.x - pos.x;
    float dy = planet.center.y - pos.y;
    float distSq = dx * dx + dy * dy;
    if (distSq < 1.0f) distSq = 1.0f;
    float gravD = sqrtf(distSq);
    float force = planet.gravity / distSq;
    vel.x += (dx / gravD) * force * timeScale;
    vel.y += (dy / gravD) * force * timeScale;

    pos.x += vel.x * timeScale;
    pos.y += vel.y * timeScale;

    // "Up" here, for telling floors from walls.
    Vec2 up = (pos - planet.center).normalize();
    float outX = up.x, outY = up.y;

    // ---- Collide with the rock itself ---------------------------------------
    // Planet::rockAt is the terrain with everything dug out of it removed, so
    // this is what lets you stand IN a hole, walk into a tunnel, and bump your
    // head on its roof — not just ride along the planet's outline. Two probes:
    //   - the feet: the center may come no closer than FOOT_R to the rock.
    //     That keeps the old stance (center 2 above the ground line, boots
    //     sunk below it onto the contact shadow — the ~40 degree ground view);
    //   - the body: circles for the chest and the helmet, for walls and
    //     ceilings.
    // Each probe that is too deep is pushed straight out along the surface
    // normal, and the velocity into the surface is removed.
    bool contact = false;
    auto pushOut = [&](Vec2 offset, float radius, bool feet) {
        Vec2 probe = pos + offset;
        float den = planet.rockAt(probe);
        if (den <= -radius) return;
        Vec2 n = planet.normalAt(probe);
        float depth = den + radius;
        pos = pos + n * depth;
        float into = vel.x * n.x + vel.y * n.y;
        if (into < 0.0f) { vel.x -= into * n.x; vel.y -= into * n.y; }
        if (feet && n.dot(up) > 0.45f) contact = true;   // a floor, not a wall
    };
    for (int pass = 0; pass < 2; pass++) {
        pushOut(Vec2(0, 0), FOOT_R, true);
        pushOut(up * BODY_UP, BODY_R, false);
        pushOut(up * HEAD_UP, HEAD_R, false);
    }

    // Stay on the ground when it falls away gently (walking downhill, over a
    // crest): if we were standing and there is floor just below, step down
    // onto it instead of going airborne for a frame at a time.
    if (!contact && wasGrounded && !jetting && jumpGrace <= 0.0f) {
        for (float k = 1.0f; k <= STEP_DOWN; k += 1.0f) {
            if (planet.rockAt(pos - up * k) > -FOOT_R) {
                pos = pos - up * (k - 1.0f);
                float outward = vel.x * up.x + vel.y * up.y;
                if (outward > 0.0f) { vel.x -= outward * up.x; vel.y -= outward * up.y; }
                contact = true;
                break;
            }
        }
    }

    grounded = contact && !jetting;
    if (grounded) {
        if (resting) {
            pos = restPos;
            vel = Vec2(0, 0);
        }
        // Surface friction damps tangential drift. WALK_DAMP is the factor for
        // one 60 fps frame; raise it to the frame's length so friction (and
        // with it the walking speed) is the same at 30, 60 or 120 fps.
        float damp = powf(WALK_DAMP, timeScale);
        vel.x *= damp;
        vel.y *= damp;

        // Recharge jetpack fuel on the ground.
        float charged = jetpackFuel + JETPACK_CHARGE * timeScale;
        jetpackFuel = charged < JETPACK_MAX_FUEL ? charged : JETPACK_MAX_FUEL;
    }

    walking = grounded && walkRequested;
    if (walking) {
        // Advance the run cycle (4 units per cycle) by the ground actually
        // covered along the surface.
        float along = fabsf(-vel.x * outY + vel.y * outX);
        walkAnim += along * timeScale * (4.0f / STRIDE_CYCLE);
    } else {
        walkAnim += timeScale * 0.05f;
    }
    walkRequested = false;

    // Pose blends (see draw): each eases toward its state so the figure flows
    // from standing into a run or off the ground instead of snapping between
    // poses.
    auto ease = [timeScale](float& v, float target, float rate) {
        float k = rate * timeScale;
        v += (target - v) * (k > 1.0f ? 1.0f : k);
    };
    ease(stride, walking ? 1.0f : 0.0f, 0.30f);
    ease(air, grounded ? 0.0f : 1.0f, 0.28f);
    ease(aim, aiming ? 1.0f : 0.0f, 0.40f);
}

// Move along the surface: dir = -1 left, +1 right (as seen with the camera
// rolled so gravity points down). On the ground this is the run. In the air it
// is a gentler steer toward a slower sideways speed — enough to drift onto a
// ledge or out of a shaft under the jetpack, which you otherwise could never
// leave, without turning a jump into flight.
void Astronaut::move(const Planet& planet, float timeScale, int dir) {
    float dx = pos.x - planet.center.x;
    float dy = pos.y - planet.center.y;
    float d  = sqrtf(dx * dx + dy * dy);
    if (d < 1.0f) return;
    // This tangent resolves to screen-right after the camera rolls gravity down.
    float tx = -dy / d;
    float ty =  dx / d;
    facing = dir;
    if (grounded) {
        // Each frame friction removes (1 - damp) of the velocity; putting
        // exactly that share of walkSpeed back makes the speed settle at
        // walkSpeed, at any frame rate.
        float push = gDebug.walkSpeed * (1.0f - powf(WALK_DAMP, timeScale));
        vel.x += tx * push * dir;
        vel.y += ty * push * dir;
        walkRequested = true;
    } else {
        float along = (vel.x * tx + vel.y * ty) * dir;          // speed the way we are steering
        float room = gDebug.walkSpeed * AIR_SPEED - along;
        if (room > 0.0f) {
            float push = AIR_ACCEL * timeScale;
            if (push > room) push = room;
            vel.x += tx * push * dir;
            vel.y += ty * push * dir;
        }
    }
}

void Astronaut::walkLeft(const Planet& planet, float timeScale)  { move(planet, timeScale, -1); }
void Astronaut::walkRight(const Planet& planet, float timeScale) { move(planet, timeScale, +1); }

void Astronaut::jump(const Planet& planet) {
    if (!grounded) return;
    float dx = pos.x - planet.center.x;
    float dy = pos.y - planet.center.y;
    float d  = sqrtf(dx * dx + dy * dy);
    if (d < 1.0f) return;
    vel.x += (dx / d) * gDebug.jumpSpeed;
    vel.y += (dy / d) * gDebug.jumpSpeed;
    grounded = false;
    jumpGrace = 8.0f;     // frames before the ground may claim us again
}

void Astronaut::jetpack(const Planet& planet, float timeScale) {
    if (jetpackFuel <= 0.0f) return;
    float dx = pos.x - planet.center.x;
    float dy = pos.y - planet.center.y;
    float d  = sqrtf(dx * dx + dy * dy);
    if (d < 1.0f) return;
    vel.x += (dx / d) * JETPACK_THRUST * timeScale;
    vel.y += (dy / d) * JETPACK_THRUST * timeScale;
    jetpackFuel -= JETPACK_DRAIN * timeScale;
    if (jetpackFuel < 0.0f) jetpackFuel = 0.0f;
    jetting = true;
}

void Astronaut::draw(Renderer2D& r, const Planet& planet) const {
    if (!active) return;

    // Stand upright: local +Y points toward planet center (feet down), same
    // convention as the cow's standAngle.
    float standAngle = atan2d(pos.y - planet.center.y, pos.x - planet.center.x) + 90.0f;

    // ---- Pose -------------------------------------------------------------
    // Run cycle: walkAnim advances 4 units per full cycle (two steps).
    float phi = walkAnim * 90.0f;
    float s = sind(phi), c = cosd(phi);
    float fwdN = c > 0 ? c : 0;        // near leg swinging forward (off the ground)
    float fwdF = c < 0 ? -c : 0;       // far leg swinging forward
    float sway = sinf(idleAnim * 0.9f);

    const float A = 36.0f;             // stride half-angle
    Pose idle{3.0f, -2.0f, -4.0f, -5.0f,
              5.0f + sway * 1.5f, 13.0f + sway * 1.5f, -7.0f - sway * 1.5f, 3.0f - sway * 1.5f, 0.0f};
    Pose run{A * s + 14.0f * fwdN, A * s - 48.0f * fwdN - 6.0f,
             -A * s + 14.0f * fwdF, -A * s - 48.0f * fwdF - 6.0f,
             -0.9f * A * s - 6.0f, -0.9f * A * s + 50.0f,
             0.9f * A * s - 6.0f, 0.9f * A * s + 50.0f, 9.0f};
    // Off the ground: a tucked jump, or legs trailing under jetpack thrust.
    Pose jump{38.0f, -28.0f, -12.0f, -50.0f, -30.0f, 0.0f, 48.0f, 84.0f, 4.0f};
    Pose jet{-6.0f, -18.0f, -15.0f, -32.0f, 16.0f, 42.0f, -12.0f, 8.0f, 7.0f};

    float airW = air;
    Pose pose = mix(idle, run, stride);
    pose = mix(pose, jetting ? jet : jump, airW);

    // Legs hang from the hips; on the ground the body drops just enough that
    // the lower foot rests on it (a long stride lowers the hips, as it should).
    Vec2 hipN(2.4f, HIP_Y), hipF(-2.4f, HIP_Y);
    Vec2 kneeN = hipN + dirDown(pose.thighN) * THIGH, footN = kneeN + dirDown(pose.shinN) * SHIN;
    Vec2 kneeF = hipF + dirDown(pose.thighF) * THIGH, footF = kneeF + dirDown(pose.shinF) * SHIN;
    float lowest = footN.y > footF.y ? footN.y : footF.y;
    float drop = (GROUND - lowest) * (1.0f - airW);
    float breathe = sinf(idleAnim) * 0.6f * (1.0f - stride) * (1.0f - airW) * (1.0f - aim);

    // Aiming the mining laser: both arms swing onto the line from the shoulder
    // to the target, and the body straightens up to shoot. The target is
    // brought into the figure's own frame (undo the stand rotation, the scale,
    // the facing mirror and the body drop).
    const Vec2 shoulder(0.5f, -8.5f);
    float aimDeg = 90.0f;
    if (aim > 0.001f) {
        Vec2 v = aimWorld - pos;
        float ca = cosd(-standAngle), sa = sind(-standAngle);
        Vec2 local((v.x * ca - v.y * sa) / SCALE * (float)facing, (v.x * sa + v.y * ca) / SCALE - drop);
        aimDeg = atan2d(local.x - shoulder.x, local.y - shoulder.y);   // from straight down, + forward
        pose.armN += (aimDeg - pose.armN) * aim;
        pose.foreN += (aimDeg - pose.foreN) * aim;
        pose.armF += (aimDeg - 16.0f - pose.armF) * aim;
        pose.foreF += (aimDeg + 14.0f - pose.foreF) * aim;
        pose.lean *= 1.0f - aim;
    }

    r.pushMatrix();
    r.translate(pos.x, pos.y);
    r.rotate(standAngle);
    r.scale(SCALE);

    // Contact shadow under the feet. Same ~64% ground-ellipse ratio as the
    // blob shadows (~40 degree view); the boots rest on its center.
    // Gone once the feet leave the ground (the soft ground shadow drawn by the
    // main loop is the one that stays below on the surface).
    r.fill(0.02f, 0.03f, 0.05f, 0.26f * (1.0f - air));
    r.ellipse(0, 30, 15.0f, 9.6f, 18);

    // Everything below is drawn facing +x; mirror it for the other direction.
    r.scale((float)facing, 1.0f);
    r.translate(0, drop);

    // Jetpack flame: out of the pack's nozzle, behind the whole figure.
    if (jetting) {
        float flick = 0.82f + 0.18f * sinf(idleAnim * 23.0f);
        r.setBlend(Renderer2D::Blend::Additive);
        r.fill(1.0f, 0.30f, 0.0f, 0.24f);
        r.ellipse(-12.0f, 20.0f + 7.0f * flick, 6.5f, 13.0f * flick, 12);
        r.fill(1.0f, 0.78f, 0.0f, 0.52f);
        r.ellipse(-12.0f, 18.0f + 4.0f * flick, 3.8f, 8.0f * flick, 12);
        r.fill(1.0f, 1.0f, 0.85f, 0.85f);
        r.ellipse(-12.0f, 16.0f + 2.0f * flick, 2.0f, 4.2f * flick, 10);
        r.setBlend(Renderer2D::Blend::Normal);
    }

    // A foot: rounded boot pointing forward from the ankle.
    auto boot = [&](Vec2 ankle) {
        fillc(r, DARK);
        r.ellipse(ankle.x + 2.2f, ankle.y + 0.6f, 5.8f, 3.4f, 14);
    };

    // ---- Far leg (shade tone: it is on the far side of the body) -----------
    fillc(r, SUIT_SHADE);
    capsule(r, hipF, kneeF, LEG_W);
    capsule(r, kneeF, footF, LEG_W * 0.92f);
    boot(footF);

    // ---- Upper body: tilts as one piece about the hips ---------------------
    r.pushMatrix();
    r.translate(0, HIP_Y + breathe);
    r.rotate(pose.lean);
    r.translate(0, -HIP_Y);

    Vec2 elbowF = shoulder + dirDown(pose.armF) * UPPER, handF = elbowF + dirDown(pose.foreF) * FORE;
    Vec2 elbowN = shoulder + dirDown(pose.armN) * UPPER, handN = elbowN + dirDown(pose.foreN) * FORE;

    // Far arm.
    fillc(r, SUIT_SHADE);
    capsule(r, shoulder, elbowF, ARM_W);
    capsule(r, elbowF, handF, ARM_W * 0.9f);
    fillc(r, DARK);
    r.circle(handF.x, handF.y, 3.3f, 12);

    // Backpack (life support + thruster), worn on the back.
    fillc(r, PACK_DARK);
    roundBox(r, -18.0f, -15.0f, -5.0f, 11.0f, 3.5f);
    fillc(r, PACK);
    roundBox(r, -18.0f, -15.0f, -8.0f, 6.0f, 3.5f);
    fillc(r, ACCENT);
    r.quad(-18.0f, -9.5f, -8.0f, -9.5f, -8.0f, -6.5f, -18.0f, -6.5f);
    fillc(r, PACK_DARK);
    r.quad(-14.5f, 10.0f, -9.5f, 10.0f, -8.5f, 14.0f, -15.5f, 14.0f);   // nozzle

    // Torso.
    fillc(r, SUIT);
    r.ellipse(0.0f, -1.0f, 10.5f, 14.5f, 26);
    fillc(r, SUIT_SHADE);
    r.ellipse(0.0f, 9.5f, 9.2f, 4.2f, 18);                               // hip shadow
    // Chest unit with a status light.
    fillc(r, PACK);
    roundBox(r, 2.5f, -6.5f, 9.5f, 1.5f, 1.8f);
    fillc(r, TEAL);
    r.circle(6.0f, -2.5f, 1.6f, 10);

    r.popMatrix();

    // ---- Near leg (drawn over the torso's lower edge) -----------------------
    fillc(r, SUIT);
    capsule(r, hipN, kneeN, LEG_W);
    capsule(r, kneeN, footN, LEG_W * 0.92f);
    fillc(r, ACCENT);
    r.circle(kneeN.x, kneeN.y, LEG_W * 0.5f, 14);                        // knee pad
    boot(footN);

    // ---- Head + near arm, back in the tilted upper-body frame --------------
    r.pushMatrix();
    r.translate(0, HIP_Y + breathe);
    r.rotate(pose.lean);
    r.translate(0, -HIP_Y);

    // Neck ring, then the helmet: a big round dome with a dark visor set
    // toward the facing side.
    fillc(r, SUIT_SHADE);
    r.ellipse(0.5f, -14.0f, 8.5f, 3.4f, 16);
    float look = sway * 0.7f * (1.0f - stride);     // idle: glance about a little
    fillc(r, SUIT);
    r.circle(1.0f, -28.0f, 15.0f, 32);
    fillc(r, PACK);
    r.circle(-9.5f, -26.0f, 3.0f, 12);                                   // comms pod
    fillc(r, VISOR);
    r.ellipse(5.6f + look, -28.0f, 9.6f, 9.2f, 26);
    // Reflection: a teal band low on the glass and a small bright glint.
    fillc(r, TEAL, 0.55f);
    r.ellipse(6.0f + look, -22.6f, 5.6f, 1.7f, 14);
    r.fill(1.0f, 1.0f, 1.0f, 0.85f);
    r.ellipse(9.6f + look, -32.4f, 3.0f, 1.9f, 12);

    // Near arm, over everything.
    fillc(r, SUIT);
    capsule(r, shoulder, elbowN, ARM_W);
    capsule(r, elbowN, handN, ARM_W * 0.9f);
    fillc(r, ACCENT);
    r.line(shoulder.x - 3.0f, shoulder.y + 2.2f, shoulder.x + 3.0f, shoulder.y + 2.2f, 2.4f);   // shoulder stripe
    fillc(r, DARK);
    r.circle(handN.x, handN.y, 3.4f, 12);

    // Mining laser: a stubby gun in the near hand, along the aim line.
    {
        Vec2 d = dirDown(aimDeg);
        Vec2 tip = handN + d * 11.0f;
        if (aim > 0.05f) {
            // Grip: square to the barrel, on whichever side hangs downward.
            Vec2 n(-d.y, d.x);
            if (n.y < 0.0f) n = n * -1.0f;
            Vec2 back = handN - d * 3.0f;
            fillc(r, DARK, aim);
            r.line(back.x, back.y, tip.x, tip.y, 5.0f);
            r.line(handN.x, handN.y, handN.x + n.x * 4.5f, handN.y + n.y * 4.5f, 3.0f);
            fillc(r, ACCENT, aim);
            r.line(handN.x + d.x * 3.0f, handN.y + d.y * 3.0f, handN.x + d.x * 5.5f, handN.y + d.y * 5.5f, 5.0f);
            r.fill(1.0f, 0.72f, 0.45f, aim);
            r.circle(tip.x, tip.y, 2.2f, 10);                            // emitter
        }
        // Publish the emitter's world position for the beam. Same chain as the
        // drawing transform: body drop, facing mirror, scale, stand rotation.
        float lx = tip.x * (float)facing * SCALE, ly = (tip.y + drop) * SCALE;
        float cs = cosd(standAngle), sn = sind(standAngle);
        muzzleWorld = Vec2(pos.x + lx * cs - ly * sn, pos.y + lx * sn + ly * cs);
    }

    r.popMatrix();
    r.popMatrix();
}
