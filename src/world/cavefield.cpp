#include "cavefield.h"
#include <algorithm>
#include <cmath>

namespace CaveField {

namespace {

using Prop = Planet::CaveProp;
using Kind = Planet::CaveProp::Kind;

struct Rgb { float r, g, b; };
Rgb rgb(const float c[3]) { return Rgb{c[0], c[1], c[2]}; }
Rgb mixc(Rgb a, Rgb b, float t) { return Rgb{a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t}; }
Rgb mul(Rgb a, float k) { return Rgb{a.r * k, a.g * k, a.b * k}; }

const Rgb GOLD{1.00f, 0.80f, 0.26f};
const Rgb GOLD_DARK{0.74f, 0.50f, 0.12f};

float smooth(float e0, float e1, float x) {
    float t = (x - e0) / (e1 - e0);
    t = t < 0 ? 0 : (t > 1 ? 1 : t);
    return t * t * (3.0f - 2.0f * t);
}

// Cheap stable 0..1 values per prop (shape variation), from its phase.
float var(const Prop& q, float salt) {
    float v = sinf(q.phase * 12.9898f + salt * 78.233f) * 43758.5453f;
    return v - floorf(v);
}

Vec2 dirVec(const Prop& q) { return Vec2(cosd(q.dir), sind(q.dir)); }

// The glowcaps' light: the world's plant color pulled toward sea-green.
Rgb glowcapColor(const Planet& p) { return mixc(rgb(p.look.flora), Rgb{0.35f, 1.0f, 0.82f}, 0.65f); }

// How far out from its root the middle of a prop is.
float reachOf(const Prop& q) {
    switch (q.kind) {
        case Prop::GoldVein: return 0.0f;
        case Prop::Glowcap:  return q.size * 0.45f;
        default:             return q.size * 0.5f;
    }
}

// Where the middle physically is (not dropped): for line of sight and lights.
Vec2 physicalCenter(const Planet& p, const Prop& q) {
    return p.center + q.local + dirVec(q) * std::max(reachOf(q), 5.0f);
}

bool glows(Kind k) { return k == Prop::Crystal || k == Prop::Glowcap; }

// The body shader's holeShade: how much of the ground's light a cave wall
// this deep keeps.
float holeShade(const Frame& f, float depth) {
    float x = depth / (f.holeDepth > 1.0f ? f.holeDepth : 1.0f);
    x = x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
    float k = (1.0f - x) * (1.0f - x);
    return f.holeFloor + (0.88f - f.holeFloor) * k;
}

}  // namespace

Vec2 root(const Planet& p, const Prop& q) {
    float len = q.local.length();
    Vec2 up = len > 1e-3f ? q.local * (1.0f / len) : Vec2(0, 1);
    return p.center + q.local - up * NearField::WALK_DROP;
}

Vec2 center(const Planet& p, const Prop& q) { return root(p, q) + dirVec(q) * reachOf(q); }

float radius(const Prop& q) {
    switch (q.kind) {
        case Prop::Column:   return std::min(q.size * 0.5f, 30.0f);
        case Prop::Crystal:  return q.size * 0.60f;
        case Prop::Glowcap:  return q.size * 0.75f;
        case Prop::GoldVein: return q.size * 0.80f;
        case Prop::Boulder:  return q.size * 0.60f;
        default:             return std::max(q.size * 0.45f, 9.0f);
    }
}

NearField::Yield yield(const Prop& q) {
    switch (q.kind) {
        case Prop::Stalactite:
        case Prop::Stalagmite: return NearField::Yield{3, q.size > 26.0f ? 2 : 1};
        case Prop::Column:     return NearField::Yield{3, 3};
        case Prop::Crystal:    return NearField::Yield{2, q.size > 26.0f ? 5 : 3};
        case Prop::Glowcap:    return NearField::Yield{4, 2};
        case Prop::GoldVein:   return NearField::Yield{5, q.size > 14.0f ? 2 : 1};
        default:               return NearField::Yield{1, 2};
    }
}

float toughness(Kind kind) {
    switch (kind) {
        case Prop::Glowcap:  return 0.35f;
        case Prop::Crystal:  return 0.95f;
        case Prop::Column:   return 1.50f;
        case Prop::GoldVein: return 1.30f;
        case Prop::Boulder:  return 0.90f;
        default:             return 0.70f;
    }
}

bool alive(const Prop& q) { return q.goneFor <= 0.0f; }

bool walled(Kind kind) {
    return kind == Prop::Stalactite || kind == Prop::Stalagmite || kind == Prop::Column;
}

void color(const Planet& p, Kind kind, float out[3]) {
    Rgb c;
    switch (kind) {
        case Prop::Crystal:  c = rgb(p.look.mineral); break;
        case Prop::Glowcap:  c = glowcapColor(p); break;
        case Prop::GoldVein: c = GOLD; break;
        case Prop::Boulder:  c = rgb(p.look.tones[3]); break;
        default:             c = mixc(rgb(p.look.tones[3]), Rgb{1, 1, 1}, 0.35f); break;   // calcite: pale rock
    }
    out[0] = c.r; out[1] = c.g; out[2] = c.b;
}

void mine(Prop& q) {
    q.hp = 1.0f;
    q.goneFor = q.kind == Prop::Glowcap ? 70.0f + var(q, 5.0f) * 60.0f : 1.0e30f;
}

void update(Planet& p, float dt, int cutting) {
    for (int i = 0; i < (int)p.caveProps.size(); i++) {
        Prop& q = p.caveProps[i];
        if (q.goneFor > 0.0f) {
            if (q.goneFor < 1.0e29f) {
                q.goneFor -= dt;
                if (q.goneFor < 0.0f) q.goneFor = 0.0f;
            }
        } else if (q.hp < 1.0f && i != cutting) {
            q.hp += dt * 0.8f;
            if (q.hp > 1.0f) q.hp = 1.0f;
        }
    }
}

int pick(const Planet& p, const Frame& f, Vec2 cursor, Vec2 from, Vec2 eye, float range, float slack) {
    int best = -1;
    float bestScore = 1e30f;
    for (int i = 0; i < (int)p.caveProps.size(); i++) {
        const Prop& q = p.caveProps[i];
        if (!alive(q)) continue;
        if (q.inside && f.inside < 0.5f) continue;            // out of sight from the surface
        Vec2 c = center(p, q);
        float rad = radius(q) + slack;
        float dc = dist(cursor.x, cursor.y, c.x, c.y);
        if (dc > rad) continue;
        if (dist(from.x, from.y, c.x, c.y) > range) continue;
        float score = dc / rad;
        if (score >= bestScore) continue;
        // A clear line from the tool: no mining through a wall.
        Vec2 target = physicalCenter(p, q);
        Vec2 d = target - eye;
        float len = d.length();
        bool blocked = false;
        for (float t = 6.0f; t < len - 6.0f; t += 6.0f) {
            if (p.rockAt(eye + d * (t / len)) > 1.5f) { blocked = true; break; }
        }
        if (blocked) continue;
        bestScore = score;
        best = i;
    }
    return best;
}

int lights(const Planet& p, const Frame& f, Light* out, int maxCount) {
    struct Cand { float d2; int i; };
    Cand near[32];
    int n = 0;
    float reach = f.reach + 220.0f;
    for (int i = 0; i < (int)p.caveProps.size(); i++) {
        const Prop& q = p.caveProps[i];
        if (!glows(q.kind) || !alive(q)) continue;
        Vec2 c = p.center + q.local;
        float dx = c.x - f.focus.x, dy = c.y - f.focus.y, d2 = dx * dx + dy * dy;
        if (d2 > reach * reach) continue;
        // Keep the nearest few, sorted.
        int k;
        if (n < 32) k = n++;
        else if (near[31].d2 <= d2) continue;
        else k = 31;
        while (k > 0 && near[k - 1].d2 > d2) { near[k] = near[k - 1]; k--; }
        near[k] = Cand{d2, i};
    }
    if (n > maxCount) n = maxCount;
    for (int k = 0; k < n; k++) {
        const Prop& q = p.caveProps[near[k].i];
        Rgb c = q.kind == Prop::Crystal ? mul(rgb(p.look.mineral), 0.42f) : mul(glowcapColor(p), 0.30f);
        out[k].pos = physicalCenter(p, q);
        out[k].radius = q.kind == Prop::Crystal ? std::min(q.size * 3.2f, 105.0f) : q.size * 5.0f;
        out[k].r = c.r; out[k].g = c.g; out[k].b = c.b;
    }
    return n;
}

void draw(Renderer2D& r, const Planet& p, const Frame& f, bool front, int hot, Part part) {
    if (p.caveProps.empty() || f.strength <= 0.01f) return;

    const Rgb mineral = rgb(p.look.mineral);
    const Rgb cap = glowcapColor(p);
    // Formations inside a cave take the tone of the wall behind them (per
    // prop, below); any out in the open are the world's own rock, dim.
    float lum = (f.light[0] + f.light[1] + f.light[2]) * (1.0f / 3.0f);
    float amb = 0.62f + 0.38f * (lum > 1.0f ? 1.0f : lum);
    const Rgb openLit = mul(rgb(p.look.tones[2]), 0.52f * amb);
    const Rgb openDark = mul(rgb(p.look.tones[0]), 0.70f * amb);
    // Rubble out at a mouth is in the open: the ground's light, lifted at
    // night like the plants so it can still be found.
    float lift = lum < 0.42f && lum > 1e-4f ? 0.42f / lum : 1.0f;
    const Rgb day{f.light[0] * lift, f.light[1] * lift, f.light[2] * lift};
    const Rgb rockLit = rgb(p.look.tones[3]), rockDark = rgb(p.look.tones[1]);
    const Rgb white{1.0f, 0.93f, 0.78f};

    float inAlpha = smooth(0.15f, 0.85f, f.inside);
    float reach2 = (f.reach + 80.0f) * (f.reach + 80.0f);

    // Two sweeps: the halos first (additive, under every shape of this
    // layer), then the shapes.
    // (The walled formations have no halos: a walled call is one sweep.)
    for (int sweep = part == Part::Walled ? 1 : 0; sweep < 2; sweep++) {
        if (sweep == 0) r.setBlend(Renderer2D::Blend::Additive);
        for (int i = 0; i < (int)p.caveProps.size(); i++) {
            const Prop& q = p.caveProps[i];
            if (q.front != front || !alive(q)) continue;
            if (walled(q.kind) != (part == Part::Walled)) continue;
            float alpha = (q.inside ? inAlpha : 1.0f) * f.strength;
            if (alpha <= 0.01f) continue;
            Vec2 at = root(p, q);
            float dx = at.x - f.focus.x, dy = at.y - f.focus.y;
            if (dx * dx + dy * dy > reach2) continue;

            float S = q.size;
            float heat = i == hot ? 1.0f - q.hp : 0.0f;
            // The wall behind a formation is the ground it is cut in, in this
            // hour's light, a shade darker for its depth (the body shader's
            // rule). Both faces sit a clear step above that wall — the shaded
            // face ~1.6x, the lit face ~2.2x — and never above the open
            // ground itself. At night their light is lifted like the plants'
            // (`day`), so they still read against a wall lit only by the
            // crystals and glowcaps.
            Rgb stoneLit = openLit, stoneDark = openDark;
            if (sweep == 1 && q.inside && !glows(q.kind) && q.kind != Prop::GoldVein) {
                float g[3];
                p.groundTone(at, g);
                Rgb open{g[0] * day.r, g[1] * day.g, g[2] * day.b};
                float depth = p.surfaceRadius(atan2d(q.local.y, q.local.x)) - q.local.length();
                float s = holeShade(f, depth);
                stoneDark = mul(open, std::min(s * 1.6f, 0.92f));
                stoneLit = mul(open, std::min(s * 2.2f, 1.0f));
            }
            if (sweep == 0) {
                if (!glows(q.kind)) continue;
                // One smooth halo: bright at the middle, nothing at the edge.
                Rgb g = q.kind == Prop::Crystal ? mineral : cap;
                float pulse = 0.88f + 0.12f * sinf(f.time * 0.5f + q.phase);
                float a0 = (q.kind == Prop::Crystal ? 0.11f : 0.10f) * alpha * pulse;
                float rad = (q.kind == Prop::Crystal ? 1.9f : 2.3f) * S;
                Vec2 c = center(p, q);
                Renderer2D::Color in{g.r, g.g, g.b, a0}, edge{g.r, g.g, g.b, 0.0f};
                const int SEG = 20;
                for (int k = 0; k < SEG; k++) {
                    float a1 = 360.0f * k / SEG, a2 = 360.0f * (k + 1) / SEG;
                    r.triangleV(c, in, Vec2(c.x + cosd(a1) * rad, c.y + sind(a1) * rad), edge,
                                Vec2(c.x + cosd(a2) * rad, c.y + sind(a2) * rad), edge);
                }
                continue;
            }

            // Flat fills. Things in front of the walking plane are darker,
            // as on the surface.
            auto stone = [&](Rgb c) {
                if (q.front) c = mul(c, 0.84f);
                c = mixc(c, white, heat * 0.75f);
                r.fill(c.r, c.g, c.b, alpha);
            };
            auto lit = [&](Rgb c) {              // emitted light: no hour, no shade
                if (q.front) c = mul(c, 0.82f);
                c = mixc(c, white, heat * 0.75f);
                r.fill(c.r, c.g, c.b, alpha);
            };
            float shake = heat > 0.0f ? sinf(f.time * 9.0f + q.phase * 3.0f) * heat * 1.2f : 0.0f;
            float shrink = 1.0f - 0.2f * heat;

            r.pushMatrix();
            r.translate(at.x, at.y);
            r.rotate(q.dir + 90.0f);             // -Y is the way it grows; +Y is into the rock
            r.translate(shake, 0.0f);

            switch (q.kind) {
            case Prop::Stalactite:
            case Prop::Stalagmite: {
                // A tapering spike with a shaded and a lit face, and a
                // shorter one or two beside it. Stalagmites are stubbier.
                bool mite = q.kind == Prop::Stalagmite;
                float L = S * shrink, w = S * (mite ? 0.30f : 0.20f) + 2.0f;
                float lean = (var(q, 1.0f) - 0.5f) * 0.22f * S;
                int extra = var(q, 2.0f) < 0.35f ? 0 : (var(q, 2.0f) < 0.75f ? 1 : 2);
                for (int k = extra; k >= 0; k--) {
                    float x = 0.0f, l = L, ww = w;
                    if (k > 0) {
                        float side = k == 1 ? (var(q, 3.0f) < 0.5f ? -1.0f : 1.0f) : (var(q, 3.0f) < 0.5f ? 1.0f : -1.0f);
                        x = side * w * (1.25f + 0.5f * var(q, 4.0f + k));
                        l = L * (0.30f + 0.32f * var(q, 6.0f + k));
                        ww = w * 0.62f;
                    }
                    float tip = x + (k == 0 ? lean : lean * 0.4f);
                    stone(stoneDark);
                    r.triangle(x - ww, 4.0f, x, 4.0f, tip, -l);
                    stone(stoneLit);
                    r.triangle(x, 4.0f, x + ww, 4.0f, tip, -l);
                }
                break;
            }
            case Prop::Column: {
                // Floor to ceiling, pinched at the waist.
                float w = std::min(std::max(S * 0.17f, 8.0f), 15.0f), waist = std::max(w * 0.32f, 2.6f);
                float mid = -S * (0.42f + 0.16f * var(q, 1.0f));
                stone(stoneDark);
                r.quad(-w, 4.0f, 0.0f, 4.0f, 0.0f, mid, -waist, mid);
                r.quad(-waist, mid, 0.0f, mid, 0.0f, -S - 4.0f, -w, -S - 4.0f);
                stone(stoneLit);
                r.quad(0.0f, 4.0f, w, 4.0f, waist, mid, 0.0f, mid);
                r.quad(0.0f, mid, waist, mid, w, -S - 4.0f, 0.0f, -S - 4.0f);
                break;
            }
            case Prop::Crystal: {
                // Leaning shards, each with a light and a dark face: the
                // surface crystal's shape, with more shards the bigger it is.
                struct Shard { float x, h, w, tilt; };
                const Shard shards[5] = {
                    {-0.44f, 0.40f + 0.12f * var(q, 4.0f), 0.20f, -0.55f},
                    { 0.46f, 0.36f + 0.12f * var(q, 5.0f), 0.20f,  0.60f},
                    {-0.20f, 0.62f + 0.12f * var(q, 1.0f), 0.26f, -0.30f},
                    { 0.22f, 0.70f + 0.14f * var(q, 2.0f), 0.26f,  0.30f},
                    { 0.00f, 1.00f,                         0.32f,  0.05f},
                };
                float L = S * shrink;
                for (int k = S > 24.0f ? 0 : 2; k < 5; k++) {
                    const Shard& sh = shards[k];
                    float x = sh.x * L, hh = sh.h * L, w = sh.w * L * 0.5f;
                    float sx = sh.tilt * hh * 0.78f, tx = sh.tilt * hh;
                    float y1 = -hh * 0.78f;
                    lit(mul(mineral, 0.62f));
                    r.quad(x - w, 3.0f, x, 3.0f, x + sx, y1, x - w + sx, y1);
                    r.triangle(x - w + sx, y1, x + sx, y1, x + tx, -hh);
                    lit(mineral);
                    r.quad(x, 3.0f, x + w, 3.0f, x + w + sx, y1, x + sx, y1);
                    r.triangle(x + sx, y1, x + w + sx, y1, x + tx, -hh);
                }
                break;
            }
            case Prop::Glowcap: {
                // Three little mushrooms: pale stems, glowing domed caps.
                struct Shroom { float x, h, cap; };
                const Shroom sh[3] = {
                    {-0.42f, 0.50f + 0.15f * var(q, 1.0f), 0.24f},
                    { 0.44f, 0.42f + 0.20f * var(q, 2.0f), 0.22f},
                    { 0.02f, 0.85f,                         0.32f},
                };
                float L = S * shrink;
                for (const Shroom& m : sh) {
                    float x = m.x * L, top = -m.h * L, cr = m.cap * L;
                    float bend = (var(q, m.x + 3.0f) - 0.5f) * 0.2f * L;
                    lit(mixc(mul(cap, 0.45f), Rgb{0.8f, 0.8f, 0.75f}, 0.25f));
                    r.line(x, 2.0f, x + bend, top, std::max(L * 0.07f, 1.2f));
                    Vec2 dome[7];
                    for (int v = 0; v < 7; v++) {
                        float t = 180.0f - 180.0f * v / 6.0f;
                        dome[v] = Vec2(x + bend + cosd(t) * cr, top - sind(t) * cr * 0.72f);
                    }
                    lit(cap);
                    r.fanPolygon(x + bend, top, dome, 7, false);
                    lit(mul(cap, 0.60f));
                    r.line(x + bend - cr, top, x + bend + cr, top, std::max(L * 0.05f, 1.0f));
                }
                break;
            }
            case Prop::GoldVein: {
                // Flecks strung through the rock just behind the wall's face.
                int glint = (int)(f.time * 0.35f + q.phase) % 5;
                for (int k = 0; k < 5; k++) {
                    float x = (k - 2.0f) * S * 0.36f + (var(q, 1.0f + k) - 0.5f) * S * 0.22f;
                    float y = 2.0f + var(q, 7.0f + k) * 8.0f;
                    float d = (2.2f + 2.2f * var(q, 13.0f + k)) * shrink;
                    Rgb c = k == glint ? mixc(GOLD, Rgb{1, 1, 1}, 0.45f) : GOLD;
                    lit(GOLD_DARK);
                    r.triangle(x - d, y, x, y + d * 0.8f, x + d, y);
                    lit(c);
                    r.triangle(x - d, y, x, y - d * 0.8f, x + d, y);
                }
                break;
            }
            default: {
                // A boulder: a lumpy dome in the terrain's own rock tones with
                // a lighter top facet (the surface boulder, without the ore).
                auto tone = [&](Rgb c) {
                    c = Rgb{c.r * day.r, c.g * day.g, c.b * day.b};
                    if (q.front) c = mixc(c, Rgb{0.10f, 0.11f, 0.16f}, 0.22f);
                    c = mixc(c, white, heat * 0.75f);
                    r.fill(c.r, c.g, c.b, alpha);
                };
                float L = S * shrink;
                Vec2 pts[8];
                const int N = 8;
                for (int v = 0; v < N; v++) {
                    float t = 180.0f - 180.0f * (float)v / (N - 1);
                    float k = 0.80f + 0.20f * var(q, (float)v + 3.0f);
                    pts[v] = Vec2(cosd(t) * L * 0.85f * k, -sind(t) * L * 0.78f * k);
                }
                tone(rockDark);
                r.fanPolygon(0, 0, pts, N, false);
                Vec2 top[5];
                for (int v = 0; v < 5; v++) {
                    Vec2 pt = pts[v + 1];
                    top[v] = Vec2(pt.x * 0.72f - L * 0.08f, pt.y * 0.80f - L * 0.08f);
                }
                tone(rockLit);
                r.fanPolygon(-L * 0.08f, -L * 0.36f, top, 5, false);
                break;
            }
            }
            r.popMatrix();
        }
        if (sweep == 0) r.setBlend(Renderer2D::Blend::Normal);
    }
}

}  // namespace CaveField
