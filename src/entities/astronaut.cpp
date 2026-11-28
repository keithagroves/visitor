#include "astronaut.h"
#include <algorithm>
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
const Rgb DARK      {0.15f, 0.18f, 0.27f};
const Rgb PACK      {0.44f, 0.48f, 0.58f};
const Rgb PACK_DARK {0.30f, 0.34f, 0.44f};
const Rgb VISOR     {0.06f, 0.09f, 0.17f};

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
void fillc(Renderer2D& r, const float* c, float a = 1.0f) { r.fill(c[0], c[1], c[2], a); }

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
    jetpackFuel   = jetpackMax;
    active        = true;
    aiming        = false;
    stride        = 0.0f;
    air           = 0.0f;
    aim           = 0.0f;
    jumpGrace     = 0.0f;
    wet           = 0.0f;
    chestDepth    = 0.0f;
    swimPose      = 0.0f;
    swimAnim      = 0.0f;
    swimGo        = 0.0f;
    stroking      = false;
    slideHeld     = false;
    sliding       = false;
    slidePose     = 0.0f;
    muzzleWorld   = position;
    swing         = -1.0f;
    blade         = 0.0f;           // stepping out with the sword drawn, it lights up
    swordAng      = SWORD_REST;
    swingDir      = 1;
    faceAim       = false;
    backpedal     = 0.0f;
}

void Astronaut::update(const Planet& planet, float timeScale) {
    if (!active) return;

    idleAnim += timeScale * 0.08f;
    if (jumpGrace > 0.0f) jumpGrace -= timeScale;

    // Standing still on the ground with nothing asked of us: remember exactly
    // where, so gravity nudging us into a slope and the collision nudging us
    // back out can't add up to a slow creep downhill.
    bool wasGrounded = grounded;
    // Sliding: asked for, on the ground and not wading. (No standing still:
    // a slope is meant to take you.)
    bool slide = slideHeld && wasGrounded && wet < SLIDE_WET && !jetting;
    bool resting = wasGrounded && !walkRequested && !jetting && jumpGrace <= 0.0f && !slide;
    Vec2 restPos = pos;

    // 1/r² gravity toward planet center (same formula as the cow's Falling state).
    float dx = planet.center.x - pos.x;
    float dy = planet.center.y - pos.y;
    float distSq = dx * dx + dy * dy;
    if (distSq < 1.0f) distSq = 1.0f;
    float gravD = sqrtf(distSq);
    float force = planet.gravity / distSq;

    // Water. How far in we are is measured from the feet up; the water takes
    // most of our weight and slows whatever we do.
    {
        Vec2 upNow(-dx / gravD, -dy / gravD);
        float feet = planet.waterDepth(pos + upNow * 2.0f);
        wet = feet <= 0.0f ? 0.0f : (feet >= WET_FULL ? 1.0f : feet / WET_FULL);
        chestDepth = feet - (BODY_UP - 2.0f);
        if (wet > 0.0f) {
            force *= 1.0f - SWIM_BUOYANCY * wet;
            float keep = powf(1.0f - (1.0f - SWIM_DRAG) * wet, timeScale);
            vel.x *= keep;
            vel.y *= keep;
        }
    }
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
    // (Sliding fast, the ground falls away further in a frame: reach further.)
    if (!contact && wasGrounded && !jetting && jumpGrace <= 0.0f) {
        float reach = STEP_DOWN + (slide ? vel.length() * 1.5f : 0.0f);
        for (float k = 1.0f; k <= reach; k += 1.0f) {
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
        float damp = powf(slide ? gDebug.slideGrip : WALK_DAMP, timeScale);
        vel.x *= damp;
        vel.y *= damp;

        // Recharge jetpack fuel on the ground.
        float charged = jetpackFuel + JETPACK_CHARGE * timeScale;
        jetpackFuel = charged < jetpackMax ? charged : jetpackMax;
    }

    sliding = slide && grounded;
    if (sliding) {
        // Facing the way we are going (feet first).
        float along = -vel.x * outY + vel.y * outX;
        if (fabsf(along) > 0.3f) facing = along > 0.0f ? 1 : -1;
    }
    walking = grounded && walkRequested;
    bool backing = false;
    if (walking) {
        // Advance the run cycle (4 units per cycle) by the ground actually
        // covered along the surface. Facing what the tool points at and
        // going the other way, the cycle runs in reverse: backing off.
        float along = -vel.x * outY + vel.y * outX;
        backing = faceAim && along * (float)facing < -0.05f;
        walkAnim += (backing ? -fabsf(along) : fabsf(along)) * timeScale * (4.0f / STRIDE_CYCLE);
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
    ease(slidePose, sliding ? 1.0f : 0.0f, sliding ? 0.35f : 0.18f);
    ease(backpedal, backing ? 1.0f : 0.0f, 0.25f);
    // The light sword's blade: out when it is drawn, in when it is put away
    // (at once if the laser is already firing: that hand is busy).
    if (swordOut || swing >= 0.0f) blade = std::min(1.0f, blade + timeScale / (60.0f * SWORD_IGNITE));
    else if (aiming) blade = 0.0f;
    else blade = std::max(0.0f, blade - timeScale / (60.0f * SWORD_RETRACT));
    // Swimming: off the bottom and in over the waist. The stroke runs faster
    // the harder we are going, and the body lies forward only while it is
    // actually travelling along.
    bool swimming = !grounded && wet > 0.55f && !jetting;
    float along = fabsf(-vel.x * outY + vel.y * outX);
    float go = along / (gDebug.walkSpeed * 0.45f);
    ease(swimPose, swimming ? 1.0f : 0.0f, 0.16f);
    ease(swimGo, go > 1.0f ? 1.0f : go, 0.10f);
    swimAnim += timeScale * (0.045f + 0.075f * swimGo + (stroking ? 0.03f : 0.0f));
    stroking = false;
}

// Swim up (dir > 0) or dive (dir < 0). Up is a lift that fades out as the
// chest reaches the surface: held, it brings you up and then keeps you there
// with your head out of the water.
void Astronaut::swim(const Planet& planet, float timeScale, int dir) {
    if (wet <= 0.0f || grounded) return;
    float dx = pos.x - planet.center.x;
    float dy = pos.y - planet.center.y;
    float d  = sqrtf(dx * dx + dy * dy);
    if (d < 1.0f) return;
    float g = planet.gravity / (d * d);
    float push;
    if (dir > 0) {
        float under = (chestDepth + 4.0f) / 8.0f;
        push = g * SWIM_LIFT * (under < 0.0f ? 0.0f : (under > 1.0f ? 1.0f : under));
    } else {
        push = -g * SWIM_DIVE * wet;
    }
    vel.x += (dx / d) * push * timeScale;
    vel.y += (dy / d) * push * timeScale;
    stroking = true;
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
    if (grounded && slideHeld && wet < SLIDE_WET) return;   // sliding: the legs are out in front
    if (!faceAim) facing = dir;
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
    vel.x += (dx / d) * gDebug.jetpackThrust * timeScale;
    vel.y += (dy / d) * gDebug.jetpackThrust * timeScale;
    jetpackFuel -= JETPACK_DRAIN * timeScale;
    if (jetpackFuel < 0.0f) jetpackFuel = 0.0f;
    jetting = true;
}

float Astronaut::swordHeld() const {
    float ahead = fmodf(swordAng * (float)facing - 90.0f + 900.0f, 360.0f) - 180.0f;   // off straight ahead
    return (90.0f + std::clamp(ahead, -HOLD_TURN, HOLD_TURN)) * (float)facing;
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
    // Sliding, feet first: the near leg out straight ahead, the far one
    // tucked under, leaning back on the near arm with the other up for
    // balance.
    Pose slidePoseP{80.0f, 86.0f, 34.0f, -62.0f, -48.0f, -30.0f, 62.0f, 112.0f, -26.0f};

    run.lean -= 13.0f * backpedal;     // backing off: upright, a touch back on the heels
    Pose pose = mix(idle, run, stride);
    pose = mix(pose, slidePoseP, slidePose);
    pose = mix(pose, jetting ? jet : jump, airW);
    // In the water: treading it upright (legs pedalling, arms sculling out
    // to the sides), or — travelling along — stretched out forward in a
    // crawl, legs trailing in a flutter kick, arms reaching past the head in
    // turn. (Arm angles are in the tilted upper body's frame: 180 = straight
    // past the head.)
    if (swimPose > 0.001f) {
        float k = sinf(swimAnim * 6.2831853f), k2 = sinf(swimAnim * 6.2831853f + 1.1f);
        Pose tread{16.0f + 20.0f * k, -22.0f + 14.0f * k2, 10.0f - 20.0f * k, -22.0f - 14.0f * k2,
                   58.0f + 16.0f * k2, 84.0f + 18.0f * k2, 44.0f - 16.0f * k2, 72.0f - 18.0f * k2, 5.0f};
        Pose crawl{-50.0f + 15.0f * k, -58.0f + 20.0f * k2, -50.0f - 15.0f * k, -58.0f - 20.0f * k2,
                   95.0f + 72.0f * k, 108.0f + 66.0f * k, 95.0f - 72.0f * k, 108.0f - 66.0f * k, 56.0f};
        pose = mix(pose, mix(tread, crawl, swimGo), swimPose);
    }

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

    // The light sword. Drawn (swordOut), it is held lit in the near hand and
    // pointed where swordAng says (at the cursor): the elbow bent, the fist
    // near that line, the blade along it. A swing is a short chop across the
    // line at swingAng, from SWING_HALF one side of it to SWING_HALF the
    // other, fast at first and easing out (swing 0..1, the arm and blade in
    // one line, the far arm coming across, the body leaning in); then
    // straight back to where it is held (1..1+SWORD_BACK). The blade flares
    // while it cuts and settles back to its held glow.
    const float SWING_HALF = 40.0f, BLADE = 40.0f;
    const float HOLD_ARM = -30.0f, HOLD_FORE = 26.0f;    // the arm's two halves, off the blade's line
    const float CUT_STRETCH = 0.25f;                     // how much longer the blade burns mid-cut
    bool swinging = swing >= 0.0f;
    bool bladeOut = swordOut || swinging || blade > 0.0f;
    // Lit, the blade shoots out of the hilt fast and slows to its full
    // length; put away, it slides back in. Coming out, it burns bright.
    float ext = swordOut || swinging ? 1.0f - (1.0f - blade) * (1.0f - blade) * (1.0f - blade)
                                     : blade * blade;     // going in: from the first frame
    float bladeLen = BLADE * ext;
    float ignite = swordOut && blade < 1.0f ? 1.0f - blade : 0.0f;
    auto towards = [](float from, float to, float k) {   // angles, the short way round
        float d = fmodf(to - from + 540.0f, 360.0f) - 180.0f;
        return from + d * k;
    };
    // Into the figure's own frame (from straight down, + forward).
    float holdDeg = swordHeld() * (float)facing;
    float swingAt = swingAng * (float)facing;            // the line the cut crosses
    auto sweep = [&](float k) {
        float e = 1.0f - (1.0f - k) * (1.0f - k);
        return swingAt + (float)swingDir * SWING_HALF * (1.0f - 2.0f * e);
    };
    float bladeDeg = holdDeg;              // which way the blade points
    float flare = 0.0f;                    // 0 held .. 1 cutting: how bright it burns
    float back = 0.0f;                     // 0..1 through the return to the hold
    if (bladeOut) {
        pose.armN = holdDeg + HOLD_ARM;
        pose.foreN = holdDeg + HOLD_FORE;
    }
    if (swinging) {
        float cutDeg = sweep(swing < 1.0f ? swing : 1.0f);
        back = swing > 1.0f ? std::min(1.0f, (swing - 1.0f) / SWORD_BACK) : 0.0f;
        float e = back * back * (3.0f - 2.0f * back);
        pose.armN = towards(cutDeg, holdDeg + HOLD_ARM, e);
        pose.foreN = towards(cutDeg, holdDeg + HOLD_FORE, e);
        bladeDeg = towards(cutDeg, holdDeg, e);
        float w = 1.0f - e;
        pose.armF += (cutDeg - 25.0f - pose.armF) * 0.7f * w;
        pose.foreF += (cutDeg - 5.0f - pose.foreF) * 0.7f * w;
        pose.lean += (8.0f - pose.lean) * w;
        flare = 1.0f - e;
        bladeLen *= 1.0f + CUT_STRETCH * flare;      // the cut reaches out
    }
    // The arm hangs off the leaning upper body: take the lean back out, so
    // the blade points where it was told to whatever the body is doing.
    const float swordLean = bladeOut ? pose.lean : 0.0f;
    if (bladeOut) {
        pose.armN += swordLean;
        pose.foreN += swordLean;
        bladeDeg += swordLean;
    }
    flare = std::max(flare, ignite * 0.8f);

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
    fillc(r, livery.suitShade);
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
    fillc(r, livery.suitShade);
    capsule(r, shoulder, elbowF, ARM_W);
    capsule(r, elbowF, handF, ARM_W * 0.9f);
    fillc(r, DARK);
    r.circle(handF.x, handF.y, 3.3f, 12);

    // Backpack (life support + thruster), worn on the back.
    fillc(r, PACK_DARK);
    roundBox(r, -18.0f, -15.0f, -5.0f, 11.0f, 3.5f);
    fillc(r, PACK);
    roundBox(r, -18.0f, -15.0f, -8.0f, 6.0f, 3.5f);
    fillc(r, livery.dye);
    r.quad(-18.0f, -9.5f, -8.0f, -9.5f, -8.0f, -6.5f, -18.0f, -6.5f);
    fillc(r, PACK_DARK);
    r.quad(-14.5f, 10.0f, -9.5f, 10.0f, -8.5f, 14.0f, -15.5f, 14.0f);   // nozzle

    // Torso.
    fillc(r, livery.suit);
    r.ellipse(0.0f, -1.0f, 10.5f, 14.5f, 26);
    fillc(r, livery.suitShade);
    r.ellipse(0.0f, 9.5f, 9.2f, 4.2f, 18);                               // hip shadow
    // Chest unit with a status light.
    fillc(r, PACK);
    roundBox(r, 2.5f, -6.5f, 9.5f, 1.5f, 1.8f);
    fillc(r, livery.glow);
    r.circle(6.0f, -2.5f, 1.6f, 10);

    r.popMatrix();

    // The light sword's hilt, hanging at the hip while it is put away.
    if (!bladeOut) {
        fillc(r, DARK);
        capsule(r, Vec2(-3.0f, 5.0f), Vec2(-7.5f, 15.0f), 3.4f);
        fillc(r, livery.dye);
        r.line(-3.6f, 6.4f, -4.5f, 8.4f, 4.4f);
        fillc(r, livery.glow, 0.8f);
        r.circle(-2.6f, 4.2f, 1.5f, 8);                                 // the emitter, dark until drawn
    }

    // ---- Near leg (drawn over the torso's lower edge) -----------------------
    fillc(r, livery.suit);
    capsule(r, hipN, kneeN, LEG_W);
    capsule(r, kneeN, footN, LEG_W * 0.92f);
    fillc(r, livery.dye);
    r.circle(kneeN.x, kneeN.y, LEG_W * 0.5f, 14);                        // knee pad
    boot(footN);

    // ---- Head + near arm, back in the tilted upper-body frame --------------
    r.pushMatrix();
    r.translate(0, HIP_Y + breathe);
    r.rotate(pose.lean);
    r.translate(0, -HIP_Y);

    // Neck ring, then the helmet: a big round dome with a dark visor set
    // toward the facing side.
    fillc(r, livery.suitShade);
    r.ellipse(0.5f, -14.0f, 8.5f, 3.4f, 16);
    float look = sway * 0.7f * (1.0f - stride);     // idle: glance about a little
    fillc(r, SUIT);                                                     // the helmet stays white
    r.circle(1.0f, -28.0f, 15.0f, 32);
    fillc(r, PACK);
    r.circle(-9.5f, -26.0f, 3.0f, 12);                                   // comms pod
    fillc(r, VISOR);
    r.ellipse(5.6f + look, -28.0f, 9.6f, 9.2f, 26);
    // Reflection: a teal band low on the glass and a small bright glint.
    fillc(r, livery.glow, 0.55f);
    r.ellipse(6.0f + look, -22.6f, 5.6f, 1.7f, 14);
    r.fill(1.0f, 1.0f, 1.0f, 0.85f);
    r.ellipse(9.6f + look, -32.4f, 3.0f, 1.9f, 12);

    // Near arm, over everything.
    fillc(r, livery.suit);
    capsule(r, shoulder, elbowN, ARM_W);
    capsule(r, elbowN, handN, ARM_W * 0.9f);
    fillc(r, livery.dye);
    r.line(shoulder.x - 3.0f, shoulder.y + 2.2f, shoulder.x + 3.0f, shoulder.y + 2.2f, 2.4f);   // shoulder stripe
    fillc(r, DARK);
    r.circle(handN.x, handN.y, 3.4f, 12);

    // The light sword in the near hand: the cut's afterglow (a fading fan
    // across the arc just swept), then the blade — one smooth halo in the
    // player's colour and a core — then the hilt over the fist. Held, it
    // burns low: a faint halo, the core tinted. Cutting, it flares: a wide
    // bright halo, a white-hot core, a bloom at the tip.
    if (bladeOut) {
        const float armLen = UPPER + FORE;
        r.setBlend(Renderer2D::Blend::Additive);
        if (swinging) {
            float k1 = swing < 1.0f ? swing : 1.0f;
            float k0 = k1 - 0.6f > 0.0f ? k1 - 0.6f : 0.0f;
            float fade = 1.0f - back * 1.6f;               // the trail goes as the arm comes back
            if (fade < 0.0f) fade = 0.0f;
            const int SEG = 10;
            for (int i = 0; i < SEG && fade > 0.0f; i++) {
                float ka = k0 + (k1 - k0) * (float)i / SEG, kb = k0 + (k1 - k0) * (float)(i + 1) / SEG;
                Vec2 da = dirDown(sweep(ka) + swordLean), db = dirDown(sweep(kb) + swordLean);
                float aA = 0.6f * fade * (float)i / SEG, aB = 0.6f * fade * (float)(i + 1) / SEG;
                Renderer2D::Color ca{livery.glow[0], livery.glow[1], livery.glow[2], aA};
                Renderer2D::Color cb{livery.glow[0], livery.glow[1], livery.glow[2], aB};
                Renderer2D::Color c0{livery.glow[0], livery.glow[1], livery.glow[2], 0.0f};
                Vec2 ia = shoulder + da * (armLen + 4.0f), oa = shoulder + da * (armLen + bladeLen);
                Vec2 ib = shoulder + db * (armLen + 4.0f), ob = shoulder + db * (armLen + bladeLen);
                r.triangleV(ia, c0, oa, ca, ob, cb);
                r.triangleV(ia, c0, ob, cb, ib, c0);
                // The line the tip drew: a thin white edge on the fan.
                Vec2 ra = shoulder + da * (armLen + bladeLen - 4.0f), rb = shoulder + db * (armLen + bladeLen - 4.0f);
                Renderer2D::Color wa{1.0f, 1.0f, 1.0f, aA}, wb{1.0f, 1.0f, 1.0f, aB}, w0{1.0f, 1.0f, 1.0f, 0.0f};
                r.triangleV(ra, w0, oa, wa, ob, wb);
                r.triangleV(ra, w0, ob, wb, rb, w0);
            }
        }
        Vec2 d = dirDown(bladeDeg);
        Vec2 base = handN + d * 3.0f, tip = handN + d * (bladeLen > 3.0f ? bladeLen : 3.0f);
        bool lit = bladeLen > 3.0f;
        if (lit) {
            fillc(r, livery.glow, 0.16f + 0.40f * flare);
            r.line(base.x, base.y, tip.x, tip.y, 7.0f + 3.0f * flare);
            r.circle(tip.x, tip.y, 3.5f + 1.5f * flare, 12);
            if (flare > 0.0f) {
                r.fill(1.0f, 1.0f, 1.0f, 0.30f * flare);       // the white heat of the cut
                r.line(base.x, base.y, tip.x, tip.y, 4.5f);
            }
            if (ignite > 0.0f) {
                // Igniting: a burst at the emitter, and the tip running out
                // ahead of the blade's glow.
                fillc(r, livery.glow, 0.5f * ignite);
                r.circle(base.x, base.y, 6.0f + 6.0f * ignite, 14);
                r.fill(1.0f, 1.0f, 1.0f, 0.7f * ignite);
                r.circle(tip.x, tip.y, 3.0f, 10);
            }
        }
        r.setBlend(Renderer2D::Blend::Normal);
        float tint = 0.45f * (1.0f - flare);                    // held: the core takes the colour
        r.fill(1.0f + (livery.glow[0] - 1.0f) * tint, 1.0f + (livery.glow[1] - 1.0f) * tint,
               1.0f + (livery.glow[2] - 1.0f) * tint, 1.0f);
        if (lit) capsule(r, base, tip, 2.6f + 0.8f * flare);
        fillc(r, DARK);
        capsule(r, handN - d * 4.0f, handN + d * 3.0f, 4.2f);
        fillc(r, livery.dye);
        r.circle(handN.x + d.x * 3.0f, handN.y + d.y * 3.0f, 2.6f, 10);
    }

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
            fillc(r, livery.dye, aim);
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
