// The locals and their trading posts: where they are (Planet::foundOutposts),
// what they do all day (update) and how they are drawn (draw). See outposts.h.
#include "outposts.h"
#include <algorithm>
#include <cctype>
#include <cmath>

namespace {

const float PI = 3.14159265f;

struct Rng {
    unsigned int state;
    explicit Rng(unsigned int s) : state(s ? s : 1) {
        for (int i = 0; i < 3; i++) next01();     // neighbouring seeds should not start alike
    }
    float next01() {
        state = state * 1664525u + 1013904223u;
        return (state >> 8) * (1.0f / 16777216.0f);
    }
    float range(float a, float b) { return a + next01() * (b - a); }
    bool chance(float p) { return next01() < p; }
    int pick(int n) { int v = (int)(next01() * n); return v >= n ? n - 1 : v; }
    unsigned int nextSeed() {
        state = state * 1664525u + 1013904223u;
        return (state >> 4) | 1u;
    }
};

float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
float smooth(float e0, float e1, float x) {
    float t = clampf((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
float wrapDeg(float a) { return std::fmod(a + 540.0f, 360.0f) - 180.0f; }

void hsv(float out[3], float h, float s, float v) {
    h = std::fmod(h, 360.0f);
    if (h < 0) h += 360.0f;
    float c = v * s, hp = h / 60.0f;
    float x = c * (1.0f - std::fabs(std::fmod(hp, 2.0f) - 1.0f));
    float r = 0, g = 0, b = 0;
    if (hp < 1)      { r = c; g = x; }
    else if (hp < 2) { r = x; g = c; }
    else if (hp < 3) { g = c; b = x; }
    else if (hp < 4) { g = x; b = c; }
    else if (hp < 5) { r = x; b = c; }
    else             { r = c; b = x; }
    float m = v - c;
    out[0] = r + m; out[1] = g + m; out[2] = b + m;
}
void toHsv(const float c[3], float& h, float& s, float& v) {
    float mx = std::fmax(c[0], std::fmax(c[1], c[2]));
    float mn = std::fmin(c[0], std::fmin(c[1], c[2]));
    float d = mx - mn;
    v = mx;
    s = mx > 1e-6f ? d / mx : 0.0f;
    h = 0;
    if (d < 1e-6f) return;
    if (mx == c[0])      h = 60.0f * std::fmod((c[1] - c[2]) / d, 6.0f);
    else if (mx == c[1]) h = 60.0f * ((c[2] - c[0]) / d + 2.0f);
    else                 h = 60.0f * ((c[0] - c[1]) / d + 4.0f);
    if (h < 0) h += 360.0f;
}

// Names for the traders: short, a little silly.
std::string keeperName(Rng& rng) {
    static const char* first[] = {"Zu", "Ok", "Mi", "Gro", "Ix", "Ya", "Qo", "Ne", "Wu", "Plo",
                                  "Bri", "Ta", "Uru", "Shi", "Do", "Kle", "Vu", "Ha", "Emo", "Ji"};
    static const char* second[] = {"vi", "ra", "po", "bu", "mm", "lo", "ti", "gs", "na", "zz",
                                   "ko", "le", "da", "x", "no", "ri"};
    static const char* tail[] = {"a", "o", "i", "u", "ek", "ul"};
    std::string n = first[rng.pick(20)];
    n += second[rng.pick(16)];
    if (rng.chance(0.3f)) n += tail[rng.pick(6)];
    return n;
}

// ---- Where the pieces of a post stand ---------------------------------------
// Ground units along from the stall (+ = the way angles grow, screen right)
// and depth behind the walking line (negative: toward the horizon edge).
const float HUT_X = -72.0f,   HUT_DEPTH = -15.0f;
const float MAST_X = 52.0f,   MAST_DEPTH = -13.0f;
const float TRADER_DEPTH = -11.0f;
const float STALL_DEPTH = -6.0f;
const float CRATE_DEPTH = -3.0f;
const float EDGE_LAMP_DEPTH = -12.0f;
// The paving's courses (rows of flagstones), by depth: from the terrain
// line at the back (groundDrop = 0 at -20.3) to a front edge out on the
// strip. Each a little deeper than the one behind it, as a nearer row would be.
const float COURSES[] = {-19.5f, -8.0f, 4.0f, 17.0f, 31.0f};
const int N_COURSES = 4;
const float KERB = 7.0f;            // the kerb's width, along the ground, at each end
// The stall: how high its counter and its awning's back edge stand, and the
// step the trader stands on behind the counter (so more than a pair of eyes
// shows over it).
const float COUNTER = 14.0f, AWNING = 63.0f, TRADER_STEP = 7.0f;
// The ground a post needs (flat, dry, clear of mesas), either side of the stall.
const float FOOTPRINT = 190.0f;
// How fast the wanderers amble, world units a second (the astronaut runs at ~145).
const float AMBLE = 24.0f;

// A piece's base point: on the ground as drawn, `along` from the stall at
// `depth`. A piece `halfWidth` wide stands on the lowest ground under it, so
// on a slope its uphill side is dug in rather than its downhill side hanging
// in the air. `angle` gets the angle it stands at.
Vec2 basePoint(const Planet& p, const Planet::Outpost& o, float along, float depth, float halfWidth, float* angle) {
    float perDeg = p.baseRadius * DEG2RAD;
    float a = o.angle + along / perDeg;
    float r = p.surfaceRadius(a);
    if (halfWidth > 0.0f) {
        float w = halfWidth / perDeg;
        r = std::fmin(r, std::fmin(p.surfaceRadius(a - w), p.surfaceRadius(a + w)));
    }
    r -= NearField::groundDrop(depth);
    if (angle) *angle = a;
    return Vec2(p.center.x + cosd(a) * r, p.center.y + sind(a) * r);
}

float alienHeight(const Planet& p, const Planet::Alien& a) { return p.folk.height * a.size; }

unsigned int dice(Planet::Outpost& o) {
    o.rng ^= o.rng << 13;
    o.rng ^= o.rng >> 17;
    o.rng ^= o.rng << 5;
    return o.rng;
}
float dice01(Planet::Outpost& o) { return (dice(o) & 0xffffffu) * (1.0f / 16777216.0f); }

// ---- Drawing ------------------------------------------------------------------
struct Rgb { float r, g, b; };
Rgb rgb(const float c[3]) { return Rgb{c[0], c[1], c[2]}; }
Rgb mixc(Rgb a, Rgb b, float t) { return Rgb{a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t}; }
Rgb mul(Rgb a, float k) { return Rgb{a.r * k, a.g * k, a.b * k}; }

// The people's accent as a dye: the crystal colors are bright and pale, which
// is right for a crystal and washed out for cloth and paint, so what they
// wear and paint with is a deeper shade of it.
Rgb dye(const float c[3]) {
    return Rgb{c[0] * (0.30f + 0.70f * c[0]), c[1] * (0.30f + 0.70f * c[1]), c[2] * (0.30f + 0.70f * c[2])};
}

// Fill colors in the hour's light (lit) or as they are (raw: emitted light,
// and the speech bubble).
struct Paint {
    Renderer2D& r;
    Rgb light;
    float alpha;
    void lit(Rgb c, float a = 1.0f) const {
        float x = c.r * light.r, y = c.g * light.g, z = c.b * light.b;
        r.fill(x > 1 ? 1 : x, y > 1 ? 1 : y, z > 1 ? 1 : z, a * alpha);
    }
    void raw(Rgb c, float a = 1.0f) const { r.fill(c.r, c.g, c.b, a * alpha); }
};

void capsule(Renderer2D& r, Vec2 a, Vec2 b, float w) {
    r.line(a.x, a.y, b.x, b.y, w);
    r.circle(a.x, a.y, w * 0.5f, 10);
    r.circle(b.x, b.y, w * 0.5f, 10);
}

void roundBox(Renderer2D& r, float x0, float y0, float x1, float y1, float rad) {
    r.quad(x0 + rad, y0, x1 - rad, y0, x1 - rad, y1, x0 + rad, y1);
    r.quad(x0, y0 + rad, x1, y0 + rad, x1, y1 - rad, x0, y1 - rad);
    r.circle(x0 + rad, y0 + rad, rad, 10);
    r.circle(x1 - rad, y0 + rad, rad, 10);
    r.circle(x1 - rad, y1 - rad, rad, 10);
    r.circle(x0 + rad, y1 - rad, rad, 10);
}

// One smooth halo: full at the middle, nothing at the rim. (Additive blend is
// the caller's business.)
void halo(Renderer2D& r, float x, float y, float radius, Rgb c, float a) {
    const int N = 20;
    Renderer2D::Color core{c.r, c.g, c.b, a}, rim{c.r, c.g, c.b, 0.0f};
    for (int i = 0; i < N; i++) {
        float a0 = (float)i / N * 2.0f * PI, a1 = (float)(i + 1) / N * 2.0f * PI;
        r.triangleV(Vec2(x, y), core, Vec2(x + cosf(a0) * radius, y + sinf(a0) * radius), rim,
                    Vec2(x + cosf(a1) * radius, y + sinf(a1) * radius), rim);
    }
}

// A soft shadow lying on the ground: flat in the middle, fading to nothing at
// the rim in one smooth step (the same construction as the ship's and the
// astronaut's blob shadows), in the ground-ellipse proportions.
void groundShadow(Renderer2D& r, float rx, float alpha) {
    const int N = 20;
    const float core = 0.45f, ry = rx * 0.64f;
    Renderer2D::Color dark{0, 0, 0, alpha}, clear{0, 0, 0, 0};
    for (int i = 0; i < N; i++) {
        float a0 = (float)i / N * 2.0f * PI, a1 = (float)(i + 1) / N * 2.0f * PI;
        Vec2 in0(cosf(a0) * rx * core, sinf(a0) * ry * core), in1(cosf(a1) * rx * core, sinf(a1) * ry * core);
        Vec2 out0(cosf(a0) * rx, sinf(a0) * ry), out1(cosf(a1) * rx, sinf(a1) * ry);
        r.triangleV(Vec2(0, 0), dark, in0, dark, in1, dark);
        r.triangleV(in0, dark, out0, clear, out1, clear);
        r.triangleV(in0, dark, out1, clear, in1, dark);
    }
}

// One of the locals, standing at the origin of the current frame (+y toward
// the planet, feet at y = 0). Drawn as a unit figure — one tall, facing +x —
// scaled to its height and mirrored to the way it faces. Three body plans
// (Folk::head): a one-piece gumdrop, a body with a head on a neck, and a low
// dome with its eyes on stalks.
void drawAlien(Renderer2D& r, const Paint& paint, const Planet::Folk& k, const Planet::Alien& a,
               float t, bool talking, float shadow) {
    const Rgb skin = rgb(k.body), shade = mul(skin, 0.70f), accent = dye(k.accent);
    const Rgb dark{0.10f, 0.11f, 0.17f}, eyeWhite{0.97f, 0.96f, 0.90f};
    float H = k.height * a.size;
    float bw = k.girth * 0.5f;                  // half the body's width, in heights
    float s = bw / 0.35f;                       // face features scale with the body
    float cyc = a.step * 2.0f * PI;
    float sw = sinf(cyc), cw = cosf(cyc);
    float m = a.moving;
    // A step's bounce while walking; breathing while standing.
    float bob = -0.03f * fabsf(sw) * m + sinf(t * 1.7f + a.phase) * 0.012f * (1.0f - m);
    float talk = talking ? 0.5f + 0.5f * sinf(t * 9.0f + a.phase) : 0.0f;
    bool blink = std::fmod(t * 0.9f + a.phase * 1.7f, 4.3f) < 0.13f;

    r.pushMatrix();
    // The shadow it stands on (`shadow` is its strength: none at night).
    if (shadow > 0.01f) groundShadow(r, H * bw * 1.7f, 0.40f * shadow * paint.alpha);
    r.scale((float)a.facing * H, H);

    auto eye = [&](float x, float y, float rad) {
        if (blink) {
            paint.lit(shade);
            r.ellipse(x, y, rad, rad * 0.16f, 12);
            return;
        }
        paint.lit(eyeWhite);
        r.circle(x, y, rad, 16);
        paint.lit(dark);
        r.circle(x + rad * 0.30f, y + rad * 0.06f, rad * 0.54f, 12);
    };
    auto leg = [&](float side, float hipX, float hipY, float w, Rgb c) {
        float ph = side > 0 ? sw : -sw;
        float lift = side > 0 ? cw : -cw;
        if (lift < 0) lift = 0;
        float x = side * hipX;
        float fx = x + 0.11f * ph * m, fy = -0.035f - 0.07f * lift * m;
        paint.lit(c);
        capsule(r, Vec2(x, hipY), Vec2(fx, fy - 0.03f), w);
        paint.lit(dark);
        r.ellipse(fx + 0.035f, fy, w * 0.85f, 0.045f, 10);
    };
    // An arm: hanging (swinging a little on the walk), or up and waving.
    auto arm = [&](Vec2 sh, float side, Rgb c, float waveK) {
        Vec2 rest = sh + Vec2(0.05f * side - 0.07f * sw * m * side, 0.20f);
        Vec2 up = sh + Vec2(0.20f + 0.06f * sinf(t * 10.0f), -0.27f + 0.03f * cosf(t * 10.0f));
        Vec2 hand = rest + (up - rest) * waveK;
        paint.lit(c);
        capsule(r, sh, hand, 0.085f);
    };
    auto antennae = [&](float x0, float x1, float y) {
        for (int i = 0; i < k.antennae; i++) {
            float x = i == 0 ? x0 : x1;
            float lean = (i == 0 ? -0.09f : 0.11f) + sinf(t * 1.3f + i * 2.0f + a.phase) * 0.02f;
            paint.lit(shade);
            r.line(x, y, x + lean, y - 0.19f, 0.03f);
            paint.lit(accent);
            r.circle(x + lean, y - 0.20f, 0.042f, 10);
        }
    };

    Vec2 bubble;            // helmet center
    float bubbleR = 0.3f;

    switch (k.head) {
    default:
    case 0: {
        // Gumdrop: body and head are one.
        leg(-1.0f, 0.42f * bw, -0.20f, 0.13f, shade);
        leg(+1.0f, 0.42f * bw, -0.20f, 0.13f, skin);
        r.translate(0, bob);
        arm(Vec2(-bw * 0.55f, -0.50f), -1.0f, shade, 0.0f);
        paint.lit(shade);
        r.ellipse(0, -0.55f, bw, 0.42f, 28);
        paint.lit(skin);
        r.ellipse(bw * 0.10f, -0.565f, bw * 0.86f, 0.385f, 28);
        paint.lit(accent);                                      // a sash, shoulder to hip
        r.quad(-bw * 0.83f, -0.33f, bw * 0.90f, -0.43f, bw * 0.86f, -0.50f, -bw * 0.79f, -0.40f);
        float ex = bw * 0.30f, ey = -0.72f;
        if (k.eyes == 1) eye(ex, ey, 0.13f * s);
        else if (k.eyes == 2) { eye(ex - 0.10f * s, ey, 0.08f * s); eye(ex + 0.10f * s, ey, 0.08f * s); }
        else { eye(ex - 0.14f * s, ey + 0.01f, 0.062f * s); eye(ex, ey - 0.07f, 0.07f * s); eye(ex + 0.14f * s, ey + 0.01f, 0.062f * s); }
        paint.lit(dark);
        r.ellipse(ex + 0.02f, -0.575f, 0.07f * s, (0.012f + 0.03f * talk) * s, 10);
        antennae(-0.10f * s, 0.12f * s, -0.94f);
        arm(Vec2(bw * 0.62f, -0.48f), 1.0f, skin, a.wave);
        bubble = Vec2(0.02f, -0.62f);
        bubbleR = std::fmax(bw, 0.34f) + 0.13f;
        break;
    }
    case 1: {
        // A small body on long legs, a wide head on a neck.
        float bx = bw * 0.82f;
        leg(-1.0f, 0.09f, -0.33f, 0.10f, shade);
        leg(+1.0f, 0.09f, -0.33f, 0.10f, skin);
        r.translate(0, bob);
        arm(Vec2(-bx * 0.45f, -0.60f), -1.0f, shade, 0.0f);
        paint.lit(shade);
        capsule(r, Vec2(0.03f, -0.64f), Vec2(0.09f, -0.80f), 0.11f);
        r.ellipse(0, -0.48f, bx, 0.22f, 24);
        paint.lit(skin);
        r.ellipse(bx * 0.10f, -0.49f, bx * 0.86f, 0.20f, 24);
        paint.lit(accent);                                      // a scarf, one end hanging
        r.ellipse(0.04f, -0.675f, 0.13f, 0.045f, 14);
        r.triangle(0.06f, -0.67f, 0.15f, -0.67f, 0.13f, -0.50f);
        paint.lit(shade);
        r.ellipse(0.11f, -0.87f, 0.20f, 0.13f, 22);
        paint.lit(skin);
        r.ellipse(0.13f, -0.875f, 0.175f, 0.115f, 22);
        float ex = 0.18f, ey = -0.89f;
        if (k.eyes == 1) eye(ex, ey, 0.075f);
        else if (k.eyes == 2) { eye(ex - 0.07f, ey, 0.05f); eye(ex + 0.07f, ey, 0.05f); }
        else { eye(ex - 0.10f, ey + 0.005f, 0.04f); eye(ex, ey - 0.03f, 0.044f); eye(ex + 0.10f, ey + 0.005f, 0.04f); }
        paint.lit(dark);
        r.ellipse(0.22f, -0.795f, 0.05f, 0.008f + 0.02f * talk, 10);
        antennae(0.05f, 0.17f, -0.985f);
        arm(Vec2(bx * 0.50f, -0.58f), 1.0f, skin, a.wave);
        bubble = Vec2(0.12f, -0.90f);
        bubbleR = 0.31f;
        break;
    }
    case 2: {
        // A low dome with its eyes up on stalks.
        float bx = bw * 1.05f;
        leg(-1.0f, 0.40f * bx, -0.20f, 0.12f, shade);
        leg(+1.0f, 0.40f * bx, -0.20f, 0.12f, skin);
        r.translate(0, bob);
        arm(Vec2(-bx * 0.60f, -0.38f), -1.0f, shade, 0.0f);
        int n = k.eyes;
        for (int i = 0; i < n; i++) {
            float off = n == 1 ? 0.0f : ((float)i - (n - 1) * 0.5f) * 0.17f;
            float sway = sinf(t * 1.1f + i * 1.9f + a.phase) * 0.015f;
            Vec2 root(0.08f + off * 0.6f, -0.60f);
            Vec2 tip(0.10f + off + sway + 0.03f * a.noticed, -0.90f - (n == 3 && i == 1 ? 0.07f : 0.0f));
            paint.lit(shade);
            r.line(root.x, root.y, tip.x, tip.y, 0.05f);
            eye(tip.x, tip.y, n == 1 ? 0.10f : 0.078f);
        }
        paint.lit(shade);
        r.ellipse(0, -0.40f, bx, 0.27f, 26);
        paint.lit(skin);
        r.ellipse(bx * 0.09f, -0.41f, bx * 0.87f, 0.245f, 26);
        paint.lit(accent);                                      // a belt
        r.quad(-bx * 0.77f, -0.235f, bx * 0.78f, -0.235f, bx * 0.90f, -0.30f, -bx * 0.89f, -0.30f);
        paint.lit(dark);
        r.ellipse(bx * 0.45f, -0.43f, 0.07f, 0.012f + 0.03f * talk, 10);
        arm(Vec2(bx * 0.66f, -0.36f), 1.0f, skin, a.wave);
        bubble = Vec2(0.10f, -0.80f);
        bubbleR = 0.36f;
        break;
    }
    }

    if (k.helmet) {
        // No air here: a glass bubble over the head, on a collar.
        paint.lit(mul(accent, 0.8f));
        r.ellipse(bubble.x, bubble.y + bubbleR * 0.90f, bubbleR * 0.56f, 0.04f, 14);
        r.fill(0.82f, 0.93f, 1.0f, 0.17f * paint.alpha);
        r.circle(bubble.x, bubble.y, bubbleR, 28);
        r.fill(1.0f, 1.0f, 1.0f, 0.50f * paint.alpha);
        r.ellipse(bubble.x + bubbleR * 0.40f, bubble.y - bubbleR * 0.52f, bubbleR * 0.24f, bubbleR * 0.11f, 10);
    }
    r.popMatrix();
}

}  // namespace

// ============================================================================
// Placement
// ============================================================================
void Planet::foundOutposts(const TerrainStyle& style) {
    outposts.clear();
    folk = Folk();
    if (isSun || landscape.size() < 3) return;

    Rng rng(seed ^ 0x666f6c6bu);

    // ---- What this world's people look like ---------------------------------
    {
        float gh, gs, gv, mh, ms, mv;
        toHsv(look.tones[2], gh, gs, gv);
        toHsv(look.mineral, mh, ms, mv);
        // Any color but the ground they stand on, or the crystal color they
        // wear and paint their stalls with — that is the one accent a post
        // adds to the scene, so it has to show against them.
        float hue = rng.range(0.0f, 360.0f);
        for (int tries = 0; tries < 6; tries++) {
            bool likeGround = gs > 0.10f && std::fabs(wrapDeg(hue - gh)) < 50.0f;
            bool likeGem = ms > 0.12f && std::fabs(wrapDeg(hue - mh)) < 48.0f;
            if (!likeGround && !likeGem) break;
            hue += 67.0f;
        }
        hsv(folk.body, hue, rng.range(0.36f, 0.58f), rng.range(0.62f, 0.80f));
        for (int c = 0; c < 3; c++) folk.accent[c] = look.mineral[c];
        folk.height = rng.range(22.0f, 31.0f);
        folk.girth = rng.range(0.56f, 0.84f);
        float e = rng.next01();
        folk.eyes = e < 0.22f ? 1 : (e < 0.72f ? 2 : 3);
        folk.head = rng.pick(3);
        folk.antennae = folk.head == 2 ? 0 : rng.pick(3);
        folk.helmet = !atmosphere;
    }

    // ---- Where they trade ------------------------------------------------------
    const float around = 2.0f * PI * baseRadius;
    const float perDeg = baseRadius * (PI / 180.0f);
    // How uneven the ground is across a post's footprint (highest minus
    // lowest) — or a huge number where any of it is wet, or on or beside a
    // mesa (and so near a cave).
    auto uneven = [&](float a) {
        float lo = 1e30f, hi = -1e30f;
        for (float x = -FOOTPRINT; x <= FOOTPRINT + 0.5f; x += 19.0f) {
            float aa = std::fmod(a + x / perDeg + 720.0f, 360.0f);    // (seedRadiusBase wants 0..360)
            if (mesaOffset(aa) > 0.0f) return 1e9f;
            float r = seedRadiusBase(aa);
            if (sea && r < baseRadius + seaLevel + 40.0f) return 1e9f;
            lo = std::fmin(lo, r);
            hi = std::fmax(hi, r);
        }
        float margin = (FOOTPRINT + 170.0f) / perDeg;
        if (mesaOffset(a + margin) > 0.0f || mesaOffset(a - margin) > 0.0f) return 1e9f;
        return hi - lo;
    };
    auto add = [&](float a) {
        Outpost o;
        o.angle = std::fmod(a + 720.0f, 360.0f);
        o.seed = rng.nextSeed();
        Rng r(o.seed);
        o.rng = o.seed | 1u;
        o.keeper = keeperName(r);
        Alien trader;
        trader.trader = true;
        trader.size = r.range(0.96f, 1.10f);
        trader.phase = r.range(0.0f, 6.28f);
        o.folk.push_back(trader);
        // One to three more wander about, each keeping to one side of the
        // stall so nobody stands between you and the trader for long.
        int n = 1 + (r.chance(0.65f) ? 1 : 0) + (r.chance(0.25f) ? 1 : 0);
        for (int i = 0; i < n; i++) {
            Alien w;
            w.size = i == 2 ? r.range(0.62f, 0.75f) : r.range(0.80f, 1.06f);     // a third is a little one
            w.phase = r.range(0.0f, 6.28f);
            float side = ((i & 1) ? -1.0f : 1.0f) * (r.chance(0.5f) ? 1.0f : -1.0f);
            float nearEnd = r.range(40.0f, 70.0f), farEnd = r.range(120.0f, Outposts::WANDER);
            w.lo = side > 0 ? nearEnd : -farEnd;
            w.hi = side > 0 ? farEnd : -nearEnd;
            w.along = w.goal = r.range(w.lo, w.hi);
            w.pause = r.range(0.0f, 3.0f);
            w.facing = r.chance(0.5f) ? 1 : -1;
            o.folk.push_back(w);
        }
        outposts.push_back(o);
    };
    auto farFromOthers = [&](float a, float apart) {
        for (const Outpost& o : outposts)
            if (std::fabs(wrapDeg(a - o.angle)) * perDeg < apart) return false;
        return true;
    };

    // The home world's first post is a short walk from where the ship drops
    // in: on the best ground a little way to the right of it (or, failing
    // that, to the left).
    bool home = style.keepClearHalf > 0.0f;
    if (home) {
        float best = 1e30f, bestA = style.keepClearAngle + 760.0f / perDeg;
        for (int side = 0; side < 2 && best > 30.0f; side++) {
            for (float x = 700.0f; x <= 1500.0f; x += 40.0f) {
                float a = style.keepClearAngle + (side ? -x : x) / perDeg;
                float u = uneven(a) + (x - 700.0f) * 0.01f;      // nearer is better, all else equal
                if (u < best) { best = u; bestA = a; }
            }
        }
        if (best < 1e8f) add(bestA);
    }

    // The rest are spread round the world, one to a sector, each on the most
    // level dry ground a handful of looks turns up.
    int want = (int)(around / 50000.0f + 0.5f);
    want = want < 1 ? 1 : (want > 5 ? 5 : want);
    float sector = 360.0f / want, turn = rng.range(0.0f, 360.0f);
    float apart = std::fmin(around / (want + 2.0f), 6000.0f);
    for (int i = 0; i < want; i++) {
        float best = 1e30f, bestA = 0.0f;
        for (int tries = 0; tries < 28; tries++) {
            float a = turn + (i + rng.range(0.12f, 0.88f)) * sector;
            if (!farFromOthers(a, apart)) continue;
            float u = uneven(a);
            if (u < best) { best = u; bestA = a; }
            if (best < 18.0f) break;
        }
        if (best < 1e8f) add(bestA);
    }
    // A world that turned up nowhere to build (all slopes and water): one
    // post on the best ground there is, so every world has somewhere to sell.
    if (outposts.empty()) {
        float best = 1e30f, bestA = 0.0f;
        for (int k = 0; k < 720; k++) {
            float u = uneven(k * 0.5f);
            if (u < best) { best = u; bestA = k * 0.5f; }
        }
        if (best < 1e8f) add(bestA);
    }

    // Nothing grows where they have built: the paving and a little past it.
    if (!outposts.empty() && !harvest.empty()) {
        harvest.erase(std::remove_if(harvest.begin(), harvest.end(), [&](const Harvestable& h) {
            for (const Outpost& o : outposts)
                if (std::fabs(wrapDeg(h.angle - o.angle)) * perDeg < Outposts::PAVED + 12.0f) return true;
            return false;
        }), harvest.end());
    }
}

// ============================================================================
namespace Outposts {

Vec2 ground(const Planet& p, const Planet::Outpost& o) {
    float r = p.surfaceRadius(o.angle);
    return Vec2(p.center.x + cosd(o.angle) * r, p.center.y + sind(o.angle) * r);
}

Vec2 traderHead(const Planet& p, const Planet::Outpost& o) {
    float a = 0.0f;
    Vec2 b = basePoint(p, o, 0.0f, TRADER_DEPTH, 0.0f, &a);
    float h = o.folk.empty() ? p.folk.height : alienHeight(p, o.folk[0]);
    if (p.folk.antennae > 0 || p.folk.head == 2) h *= 1.12f;
    return b + Vec2(cosd(a), sind(a)) * (h + TRADER_STEP);
}

int nearest(const Planet& p, Vec2 world, float* along) {
    float a = atan2d(world.y - p.center.y, world.x - p.center.x);
    float perDeg = p.baseRadius * DEG2RAD;
    int best = -1;
    float bestD = 1e30f;
    for (int i = 0; i < (int)p.outposts.size(); i++) {
        float d = wrapDeg(p.outposts[i].angle - a) * perDeg;
        if (std::fabs(d) < std::fabs(bestD)) { bestD = d; best = i; }
    }
    if (along) *along = best >= 0 ? bestD : 0.0f;
    return best;
}

int inReach(const Planet& p, Vec2 world) {
    int best = -1;
    float bestD = TRADE_REACH;
    for (int i = 0; i < (int)p.outposts.size(); i++) {
        Vec2 g = ground(p, p.outposts[i]);
        float d = dist(world.x, world.y, g.x, g.y);
        if (d < bestD) { bestD = d; best = i; }
    }
    return best;
}

bool blocksDig(const Planet& p, Vec2 world, float radius) {
    for (const Planet::Outpost& o : p.outposts) {
        Vec2 g = ground(p, o);
        if (dist(world.x, world.y, g.x, g.y) < NO_DIG + radius) return true;
    }
    return false;
}

bool onPaving(const Planet& p, Vec2 world, float margin) {
    float along = 0.0f;
    return nearest(p, world, &along) >= 0 && std::fabs(along) < PAVED + margin;
}

void update(Planet& p, float dt, Vec2 visitor, bool present, Vec2 toSun) {
    if (p.outposts.empty()) return;
    // Open by day, shut by night (whatever is happening, everywhere: the
    // rule must hold for a post nobody is watching too).
    for (Planet::Outpost& o : p.outposts) {
        o.closed = cosd(o.angle) * toSun.x + sind(o.angle) * toSun.y < CLOSE_SUN;
        float want = o.closed ? 1.0f : 0.0f, k = dt / 1.2f;
        o.shut = std::fabs(want - o.shut) <= k ? want : o.shut + (want > o.shut ? k : -k);
    }
    float perDeg = p.baseRadius * DEG2RAD;
    float va = atan2d(visitor.y - p.center.y, visitor.x - p.center.x);
    float vr = dist(visitor.x, visitor.y, p.center.x, p.center.y);
    auto ease = [dt](float& v, float target, float rate) {
        float k = rate * dt;
        v += (target - v) * (k > 1.0f ? 1.0f : k);
    };
    for (Planet::Outpost& o : p.outposts) {
        // Where the visitor is from here: along the ground, and above it.
        float vx = wrapDeg(va - o.angle) * perDeg;
        if (std::fabs(vx) > 3000.0f) { o.near = false; continue; }     // nobody is watching
        float vh = vr - p.surfaceRadius(va);
        bool here = present && std::fabs(vx) < 420.0f && std::fabs(vh) < 170.0f;
        // The trader waves when you first come up.
        bool close = here && std::fabs(vx) < 250.0f;
        if (close && !o.near) o.hail = 2.4f;
        o.near = close;
        if (o.hail > 0.0f) o.hail -= dt;

        for (Planet::Alien& a : o.folk) {
            float rel = vx - a.along;                 // the visitor, from this one
            if (a.trader) {
                bool notice = here && std::fabs(rel) < 280.0f;
                if (notice && std::fabs(rel) > 6.0f) a.facing = rel > 0.0f ? 1 : -1;
                ease(a.noticed, notice ? 1.0f : 0.0f, 6.0f);
                ease(a.wave, o.hail > 0.0f ? 1.0f : 0.0f, 9.0f);
                continue;
            }
            // A wanderer: walk to somewhere on its stretch, stand a while,
            // pick somewhere else. It stops to look at a visitor who comes
            // right up to it.
            bool notice = here && std::fabs(rel) < 62.0f;
            bool walking = false;
            if (notice) {
                if (std::fabs(rel) > 6.0f) a.facing = rel > 0.0f ? 1 : -1;
                if (a.pause < 0.7f) a.pause = 0.7f;
            } else if (a.pause > 0.0f) {
                a.pause -= dt;
            } else {
                float d = a.goal - a.along;
                float stepLen = AMBLE * a.size * dt;
                if (std::fabs(d) <= stepLen) {
                    a.along = a.goal;
                    a.pause = 1.2f + dice01(o) * 4.5f;
                    a.goal = a.lo + dice01(o) * (a.hi - a.lo);
                } else {
                    a.facing = d > 0.0f ? 1 : -1;
                    a.along += a.facing * stepLen;
                    a.step += stepLen / (alienHeight(p, a) * 0.95f);     // a stride is about a body height
                    walking = true;
                }
            }
            ease(a.moving, walking ? 1.0f : 0.0f, 9.0f);
            ease(a.noticed, notice ? 1.0f : 0.0f, 6.0f);
            ease(a.wave, 0.0f, 9.0f);
        }
    }
}

void draw(Renderer2D& r, const Planet& p, const NearField::Frame& f, float pxPerUnit, int active) {
    if (p.outposts.empty() || f.strength <= 0.01f) return;
    float perDeg = p.baseRadius * DEG2RAD;
    float span = f.halfSpanDeg + 400.0f / perDeg;
    float t = f.time / 3.0f;                                  // seconds (the frame clock runs at 3x)

    // The hour's light, lifted at night like the plants' so a post can still
    // be found; and how far the lamps are on (0 by day .. 1 at night).
    float lum = (f.light[0] + f.light[1] + f.light[2]) * (1.0f / 3.0f);
    float lift = lum < 0.42f && lum > 1e-4f ? 0.42f / lum : 1.0f;
    Paint paint{r, Rgb{f.light[0] * lift, f.light[1] * lift, f.light[2] * lift}, f.strength};
    float night = 1.0f - smooth(0.30f, 0.62f, lum);

    // The palette: the world's own pale rock for the hut, a dark timber for
    // everything built of poles and planks, the people's accent for paint
    // and cloth, a pale cloth beside it, and a warm lamp.
    const Rgb accent = dye(p.folk.accent);
    const Rgb wall = rgb(p.look.tones[3]), wallShade = rgb(p.look.tones[2]);
    const Rgb timber = mixc(rgb(p.look.tones[0]), Rgb{0.36f, 0.25f, 0.18f}, 0.6f);
    const Rgb plank = mixc(timber, Rgb{0.80f, 0.66f, 0.48f}, 0.42f);
    const Rgb cloth = mixc(rgb(p.folk.accent), Rgb{1.0f, 0.97f, 0.90f}, 0.80f);
    const Rgb ink{0.10f, 0.11f, 0.16f};
    const Rgb lamp = mixc(rgb(p.folk.accent), Rgb{1.0f, 0.86f, 0.52f}, 0.6f);

    for (int idx = 0; idx < (int)p.outposts.size(); idx++) {
        const Planet::Outpost& o = p.outposts[idx];
        if (std::fabs(wrapDeg(o.angle - f.viewAngle)) > span) continue;
        float a = 0.0f;
        // Which side the sun is on, for the hut's lit face.
        float sunSide = (-sind(o.angle) * f.toSun.x + cosd(o.angle) * f.toSun.y) >= 0.0f ? 1.0f : -1.0f;
        // Points that glow, collected for one additive pass at the end.
        Vec2 glowAt[4];
        float glowR[4], glowA[4];
        int glows = 0;
        auto toWorld = [&](Vec2 base, float ang, float lx, float ly) {
            float c = cosd(ang + 90.0f), s = sind(ang + 90.0f);
            return Vec2(base.x + lx * c - ly * s, base.y + lx * s + ly * c);
        };

        // ---- The edge of town: a stone lamp at each end of the paving ----------
        // (Behind the walking line, so you walk past in front of it.)
        for (int side = -1; side <= 1; side += 2) {
            const float LH = 21.0f, LW = 3.6f;
            Vec2 b = basePoint(p, o, side * (PAVED - KERB - 12.0f), EDGE_LAMP_DEPTH, 0.0f, &a);   // (just inside the kerb)
            if (p.overWater(a)) continue;
            r.pushMatrix();
            r.translate(b.x, b.y);
            r.rotate(a + 90.0f);
            paint.lit(wallShade);
            r.quad(-LW - 1.2f, 0, LW + 1.2f, 0, LW, -LH, -LW, -LH);
            paint.lit(wall);
            r.quad(-LW * 0.2f + sunSide * 0.8f, 0, LW + 1.2f * (sunSide > 0 ? 1.0f : 0.0f), 0,
                   LW * (sunSide > 0 ? 1.0f : 0.2f), -LH, -LW * 0.2f, -LH);
            paint.lit(accent);                                  // a painted band
            r.quad(-LW - 0.6f, -LH * 0.42f, LW + 0.6f, -LH * 0.42f, LW + 0.5f, -LH * 0.58f, -LW - 0.5f, -LH * 0.58f);
            // The lamp on top: lit glass under a dark cap.
            paint.lit(ink);
            r.quad(-LW - 0.8f, -LH - 6.5f, LW + 0.8f, -LH - 6.5f, LW - 0.6f, -LH - 8.5f, -LW + 0.6f, -LH - 8.5f);
            paint.raw(mixc(mul(lamp, 0.75f), lamp, night));
            r.quad(-LW + 0.6f, -LH, LW - 0.6f, -LH, LW - 0.2f, -LH - 6.5f, -LW + 0.2f, -LH - 6.5f);
            r.popMatrix();
            glowAt[glows] = toWorld(b, a, 0.0f, -LH - 3.5f);
            glowR[glows] = 16.0f;
            glowA[glows++] = (0.05f + 0.36f * night) * (0.86f + 0.14f * sinf(t * 1.3f + side + o.angle));
        }

        // ---- The hut: a dome of the world's pale rock --------------------------
        {
            const float W = 36.0f, Hh = 42.0f;
            Vec2 b = basePoint(p, o, HUT_X, HUT_DEPTH, W, &a);
            r.pushMatrix();
            r.translate(b.x, b.y);
            r.rotate(a + 90.0f);
            const int N = 16;
            Vec2 pts[N];
            for (int v = 0; v < N; v++) {
                float ang = 180.0f - 180.0f * (float)v / (N - 1);
                pts[v] = Vec2(cosd(ang) * W, -sind(ang) * Hh);
            }
            paint.lit(wallShade);
            r.fanPolygon(0, 0, pts, N, false);
            for (int v = 0; v < N; v++) pts[v] = Vec2(pts[v].x * 0.84f + sunSide * W * 0.13f, pts[v].y * 0.93f);
            paint.lit(wall);
            r.fanPolygon(sunSide * W * 0.13f, 0, pts, N, false);
            // A painted band round it, a door on the stall's side, a round window.
            paint.lit(accent);
            r.quad(-W * 0.955f, -Hh * 0.27f, W * 0.955f, -Hh * 0.27f, W * 0.925f, -Hh * 0.36f, -W * 0.925f, -Hh * 0.36f);
            paint.lit(mixc(ink, wallShade, 0.25f));
            r.quad(W * 0.20f, 0, W * 0.58f, 0, W * 0.58f, -15.0f, W * 0.20f, -15.0f);
            r.circle(W * 0.39f, -15.0f, W * 0.19f, 14);
            Rgb glass = mixc(mul(lamp, 0.42f), lamp, night);
            paint.raw(mixc(mul(glass, lum > 1.0f ? 1.0f : lum), glass, night));
            r.circle(-W * 0.42f, -Hh * 0.56f, 5.0f, 14);
            // A spike on top with a bead on it.
            paint.lit(timber);
            r.line(-W * 0.08f, -Hh * 0.98f, -W * 0.08f, -Hh - 13.0f, 1.8f);
            paint.lit(accent);
            r.circle(-W * 0.08f, -Hh - 14.0f, 2.6f, 10);
            r.popMatrix();
            if (night > 0.02f) {
                glowAt[glows] = toWorld(b, a, -W * 0.42f, -Hh * 0.56f);
                glowR[glows] = 15.0f;
                glowA[glows++] = 0.30f * night;
            }
        }

        // ---- The mast and its lantern -------------------------------------------
        {
            const float M = 98.0f;
            Vec2 b = basePoint(p, o, MAST_X, MAST_DEPTH, 0.0f, &a);
            r.pushMatrix();
            r.translate(b.x, b.y);
            r.rotate(a + 90.0f);
            paint.lit(timber);
            r.line(0, 0, 0, -M, 2.8f);
            r.line(-1.0f, -M + 5.0f, 14.0f, -M + 5.0f, 2.0f);
            r.line(13.0f, -M + 5.0f, 13.0f, -M + 10.0f, 1.0f);
            // A pennant, stirring.
            float flap = sinf(t * 2.3f + o.angle) * 2.2f;
            paint.lit(accent);
            r.triangle(-1.2f, -M - 1.0f, -1.2f, -M + 12.0f, -19.0f, -M + 5.0f + flap);
            // The lantern: lit glass under a dark cap. Emitted light, so it
            // keeps its color at any hour.
            paint.lit(ink);
            r.quad(8.0f, -M + 9.0f, 18.0f, -M + 9.0f, 17.0f, -M + 11.5f, 9.0f, -M + 11.5f);
            paint.raw(mixc(mul(lamp, 0.80f), lamp, night));
            r.quad(9.5f, -M + 11.5f, 16.5f, -M + 11.5f, 15.5f, -M + 20.0f, 10.5f, -M + 20.0f);
            r.popMatrix();
            // Its halo: faint by day, bright at night, breathing slowly, and
            // never smaller than a few pixels, so it marks the post from the
            // air.
            float pulse = 0.82f + 0.18f * sinf(t * 1.6f + o.angle);
            glowAt[glows] = toWorld(b, a, 13.0f, -M + 16.0f);
            glowR[glows] = std::fmax(22.0f, 8.0f / (pxPerUnit > 1e-4f ? pxPerUnit : 1e-4f));
            glowA[glows++] = (0.10f + 0.48f * night) * pulse;
        }

        // ---- The stall, back half: the awning's poles --------------------------
        Vec2 stallB = basePoint(p, o, 0.0f, STALL_DEPTH, 24.0f, &a);
        float stallA = a;
        r.pushMatrix();
        r.translate(stallB.x, stallB.y);
        r.rotate(stallA + 90.0f);
        paint.lit(timber);
        r.line(-25.0f, 0, -25.0f, -AWNING + 5.0f, 2.6f);
        r.line(25.0f, 0, 25.0f, -AWNING + 5.0f, 2.6f);
        r.popMatrix();

        // ---- The trader, behind the counter (indoors once it is shut) --------
        if (!o.folk.empty() && o.shut < 0.5f) {
            Vec2 b = basePoint(p, o, 0.0f, TRADER_DEPTH, 0.0f, &a);
            r.pushMatrix();
            r.translate(b.x, b.y);
            r.rotate(a + 90.0f);
            r.translate(0, -TRADER_STEP);         // on the step behind the counter
            drawAlien(r, paint, p.folk, o.folk[0], t, idx == active, 0.0f);
            r.popMatrix();
        }

        // ---- The stall, front half: counter, wares, awning ---------------------
        r.pushMatrix();
        r.translate(stallB.x, stallB.y);
        r.rotate(stallA + 90.0f);
        paint.lit(timber);                                          // the counter's face
        const float CT = COUNTER;                                   // the counter's top
        r.quad(-23.0f, 0, 23.0f, 0, 23.0f, -CT + 2.5f, -23.0f, -CT + 2.5f);
        paint.lit(plank);                                           // ...and its top
        r.quad(-25.5f, -CT + 2.5f, 25.5f, -CT + 2.5f, 24.5f, -CT, -24.5f, -CT);
        paint.lit(accent);                                          // a cloth over the front
        r.quad(-8.5f, -CT + 2.5f, 8.5f, -CT + 2.5f, 8.5f, -5.5f, -8.5f, -5.5f);
        r.triangle(-8.5f, -5.5f, 8.5f, -5.5f, 0.0f, -1.5f);
        // Wares on the counter, out at its ends: a couple of the world's
        // crystals, a jar.
        const Rgb gem = rgb(p.look.mineral);
        paint.lit(mul(gem, 0.62f));
        r.triangle(-22.5f, -CT, -19.0f, -CT, -21.0f, -CT - 9.0f);
        r.triangle(-19.5f, -CT, -16.0f, -CT, -16.5f, -CT - 6.0f);
        paint.lit(gem);
        r.triangle(-21.0f, -CT, -19.0f, -CT, -21.0f, -CT - 9.0f);
        r.triangle(-18.0f, -CT, -16.0f, -CT, -16.5f, -CT - 6.0f);
        paint.lit(plank);
        r.ellipse(19.5f, -CT - 4.0f, 4.0f, 4.2f, 14);
        paint.lit(timber);
        r.quad(17.5f, -CT - 8.0f, 21.5f, -CT - 8.0f, 21.0f, -CT - 10.0f, 18.0f, -CT - 10.0f);
        // The awning: stripes of the accent and a pale cloth, with a
        // scalloped front edge.
        const int STRIPES = 6;
        for (int k = 0; k < STRIPES; k++) {
            float u0 = (float)k / STRIPES, u1 = (float)(k + 1) / STRIPES;
            float xb0 = -30.0f + 60.0f * u0, xb1 = -30.0f + 60.0f * u1;      // back edge (higher)
            float xf0 = -33.0f + 66.0f * u0, xf1 = -33.0f + 66.0f * u1;      // front edge (lower)
            Rgb c = (k & 1) ? cloth : accent;
            paint.lit(c);
            r.quad(xb0, -AWNING, xb1, -AWNING, xf1, -AWNING + 9.0f, xf0, -AWNING + 9.0f);
            paint.lit(mul(c, 0.80f));
            r.triangle(xf0, -AWNING + 9.0f, xf1, -AWNING + 9.0f, (xf0 + xf1) * 0.5f, -AWNING + 14.0f);
        }
        // Shut for the night: a shutter of planks comes down from under the
        // awning over the counter, and a sign with the moon on it hangs from
        // the awning's edge.
        if (o.shut > 0.0f) {
            float top = -AWNING + 9.0f, bottom = -CT;
            float low = top + (bottom - top) * o.shut;
            paint.lit(timber);
            r.quad(-24.0f, top, 24.0f, top, 24.0f, low, -24.0f, low);
            paint.lit(plank);
            for (float y = top + 5.0f; y < low - 1.0f; y += 6.0f) r.quad(-23.0f, y, 23.0f, y, 23.0f, y + 1.2f, -23.0f, y + 1.2f);
            if (o.shut > 0.6f) {
                float swing = sinf(t * 1.3f + o.angle) * 3.0f;
                r.pushMatrix();
                r.translate(0.0f, top + 1.0f);
                r.rotate(swing);
                paint.lit(ink);
                r.line(-5.0f, 0.0f, -5.0f, 6.0f, 0.8f);
                r.line(5.0f, 0.0f, 5.0f, 6.0f, 0.8f);
                paint.lit(plank);
                roundBox(r, -8.0f, 6.0f, 8.0f, 18.0f, 2.0f);
                paint.lit(accent);                                   // a crescent moon: closed for the night
                r.circle(0.0f, 12.0f, 4.0f, 16);
                paint.lit(plank);
                r.circle(1.8f, 10.8f, 3.4f, 16);
                r.popMatrix();
            }
        }
        r.popMatrix();

        // ---- Crates and a pot beside it ---------------------------------------------
        {
            Vec2 b = basePoint(p, o, 31.0f, CRATE_DEPTH, 8.0f, &a);
            r.pushMatrix();
            r.translate(b.x, b.y);
            r.rotate(a + 90.0f);
            paint.lit(timber);
            r.quad(-7.0f, 0, 7.0f, 0, 7.0f, -12.0f, -7.0f, -12.0f);
            paint.lit(plank);
            r.quad(-7.0f, -12.0f, 7.0f, -12.0f, 6.0f, -14.5f, -6.0f, -14.5f);
            r.quad(-7.0f, -5.0f, 7.0f, -5.0f, 7.0f, -7.0f, -7.0f, -7.0f);
            r.popMatrix();
            b = basePoint(p, o, -34.0f, CRATE_DEPTH, 6.0f, &a);
            r.pushMatrix();
            r.translate(b.x, b.y);
            r.rotate(a + 90.0f);
            paint.lit(timber);
            r.ellipse(0, -6.5f, 6.5f, 6.5f, 16);
            paint.lit(plank);
            r.ellipse(1.2f, -7.2f, 4.6f, 5.0f, 14);
            paint.lit(timber);
            r.quad(-3.5f, -12.0f, 3.5f, -12.0f, 4.2f, -14.5f, -4.2f, -14.5f);
            r.popMatrix();
        }

        // ---- The others, out on the walking line (home for the night) ----------
        for (size_t k = 1; k < o.folk.size() && o.shut < 0.5f; k++) {
            const Planet::Alien& w = o.folk[k];
            Vec2 b = basePoint(p, o, w.along, 0.0f, 0.0f, &a);
            r.pushMatrix();
            r.translate(b.x, b.y);
            r.rotate(a + 90.0f);
            drawAlien(r, paint, p.folk, w, t, false, 1.0f - night);
            r.popMatrix();
        }

        // ---- "Trade?": a speech bubble over the trader -------------------------
        if (idx == active && !o.folk.empty() && o.shut < 0.5f) {
            // (Over the awning, so it reads as said from the stall and never
            // sits on the stripes.)
            float bobY = sinf(t * 3.0f) * 1.2f;
            r.pushMatrix();
            r.translate(stallB.x, stallB.y);
            r.rotate(stallA + 90.0f);
            r.translate(0, -AWNING - 4.0f + bobY);
            paint.raw(Rgb{0.97f, 0.97f, 0.93f});
            roundBox(r, -13.0f, -22.0f, 13.0f, -5.0f, 5.0f);
            r.triangle(-3.5f, -5.5f, 3.5f, -5.5f, 0.0f, 0.5f);
            // The mark for credits: a two-tone teal diamond (the HUD's).
            paint.raw(Rgb{0.12f, 0.52f, 0.50f});
            r.triangle(0, -19.5f, -5.5f, -13.5f, 0, -7.5f);
            paint.raw(Rgb{0.30f, 0.86f, 0.80f});
            r.triangle(0, -19.5f, 5.5f, -13.5f, 0, -7.5f);
            r.popMatrix();
        }

        // ---- Lamplight -------------------------------------------------------------
        if (glows > 0) {
            r.setBlend(Renderer2D::Blend::Additive);
            for (int g = 0; g < glows; g++) halo(r, glowAt[g].x, glowAt[g].y, glowR[g], lamp, glowA[g] * f.strength);
            r.setBlend(Renderer2D::Blend::Normal);
        }
    }
}

void drawGround(Renderer2D& r, const Planet& p, const NearField::Frame& f) {
    if (p.outposts.empty() || f.strength <= 0.01f) return;
    float perDeg = p.baseRadius * DEG2RAD;
    float span = f.halfSpanDeg + (PAVED + 40.0f) / perDeg;
    // Flat on the ground in the hour's light, like the ground itself (not
    // lifted at night, as what stands on it is).
    Paint paint{r, Rgb{f.light[0], f.light[1], f.light[2]}, f.strength};
    const Rgb stone = mixc(rgb(p.look.tones[3]), Rgb{0.86f, 0.82f, 0.74f}, 0.45f);
    const Rgb stoneB = mul(stone, 0.91f);                    // every so often a darker one
    const Rgb joint = mixc(mul(stone, 0.58f), rgb(p.look.tones[1]), 0.35f);
    const Rgb kerb = dye(p.folk.accent);
    const Rgb kerbTop = mixc(kerb, Rgb{1.0f, 1.0f, 1.0f}, 0.22f);

    // A point on the ground plane: `x` along the ground from the stall, at a depth.
    auto at = [&](const Planet::Outpost& o, float x, float depth) {
        float a = o.angle + x / perDeg;
        float rr = p.surfaceRadius(a) - NearField::groundDrop(depth);
        return Vec2(p.center.x + cosd(a) * rr, p.center.y + sind(a) * rr);
    };
    // A patch of it, x0..x1 by d0..d1, in pieces no longer than ~20 along
    // so it follows the ground's curve.
    auto patch = [&](const Planet::Outpost& o, float x0, float x1, float d0, float d1) {
        int n = std::max(1, (int)std::ceil((x1 - x0) / 20.0f));
        for (int i = 0; i < n; i++) {
            float u0 = x0 + (x1 - x0) * i / n, u1 = x0 + (x1 - x0) * (i + 1) / n;
            Vec2 a0 = at(o, u0, d0), a1 = at(o, u1, d0), b1 = at(o, u1, d1), b0 = at(o, u0, d1);
            r.quad(a0.x, a0.y, a1.x, a1.y, b1.x, b1.y, b0.x, b0.y);
        }
    };
    const float GAP = 1.3f;                                  // the joints, along the ground
    const float GAP_D = 1.6f;                                // ...and between courses (depth)
    for (const Planet::Outpost& o : p.outposts) {
        if (std::fabs(wrapDeg(o.angle - f.viewAngle)) > span) continue;
        float lo = -PAVED, hi = PAVED;
        // The bed the stones are set in: the joints' color.
        paint.lit(joint);
        patch(o, lo, hi, COURSES[0], COURSES[N_COURSES]);
        // The stones, course by course, staggered, each its own length.
        for (int c = 0; c < N_COURSES; c++) {
            unsigned int h = o.seed * 2654435761u + (unsigned int)c * 40503u + 17u;
            auto rnd = [&h]() { h ^= h << 13; h ^= h >> 17; h ^= h << 5; return (h & 0xffffu) / 65536.0f; };
            float d0 = COURSES[c] + (c == 0 ? 0.0f : GAP_D * 0.5f);
            float d1 = COURSES[c + 1] - (c == N_COURSES - 1 ? 0.0f : GAP_D * 0.5f);
            float x = lo + KERB - rnd() * 22.0f;
            while (x < hi - KERB) {
                float len = 22.0f + rnd() * 18.0f;
                float x0 = std::max(x, lo + KERB) + GAP * 0.5f, x1 = std::min(x + len, hi - KERB) - GAP * 0.5f;
                bool dark = rnd() < 0.3f;
                x += len;
                if (x1 - x0 < 3.0f) continue;
                if (p.overWater(o.angle + (x0 + x1) * 0.5f / perDeg)) continue;
                paint.lit(dark ? stoneB : stone);
                patch(o, x0, x1, d0, d1);
            }
        }
        // The kerbs, in the people's paint: where their ground ends.
        for (int side = -1; side <= 1; side += 2) {
            float x0 = side < 0 ? lo : hi - KERB, x1 = side < 0 ? lo + KERB : hi;
            if (p.overWater(o.angle + (x0 + x1) * 0.5f / perDeg)) continue;
            paint.lit(kerb);
            patch(o, x0, x1, COURSES[0], COURSES[N_COURSES]);
            // ...lit along its top edge.
            float xi = side < 0 ? x1 - 2.2f : x0, xo = side < 0 ? x1 : x0 + 2.2f;
            paint.lit(kerbTop);
            patch(o, std::min(xi, xo), std::max(xi, xo), COURSES[0], COURSES[N_COURSES]);
        }
    }
}

std::string phrase(const Planet::Outpost& o) {
    static const char* syl[] = {"vek", "sa", "lu", "mo", "ix", "ta", "gru", "ni", "ol", "qua",
                                "zep", "bi", "wo", "kra", "shi", "um", "de", "pli", "ax", "oo"};
    Rng rng(o.seed ^ 0x7068726eu);
    std::string out;
    int words = 2 + rng.pick(3);
    for (int w = 0; w < words; w++) {
        if (w) out += " ";
        int n = 1 + rng.pick(3);
        for (int k = 0; k < n; k++) out += syl[rng.pick(20)];
    }
    if (!out.empty()) out[0] = (char)std::toupper((unsigned char)out[0]);
    out += rng.chance(0.3f) ? "!" : ".";
    return out;
}

}  // namespace Outposts
