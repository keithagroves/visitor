#include "nearfield.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace NearField {

namespace {

using Kind = Planet::Harvestable::Kind;

float wrapDeg(float a) { return std::fmod(a + 540.0f, 360.0f) - 180.0f; }

float smooth(float e0, float e1, float x) {
    float t = (x - e0) / (e1 - e0);
    t = t < 0 ? 0 : (t > 1 ? 1 : t);
    return t * t * (3.0f - 2.0f * t);
}

// Cheap stable 0..1 values per item (shape variation), from its phase.
float var(const Planet::Harvestable& h, float salt) {
    float v = sinf(h.phase * 12.9898f + salt * 78.233f) * 43758.5453f;
    return v - floorf(v);
}

// The items within the view span: the index of the first, and how many (the
// caller walks them modulo the count, since the span may straddle 0 degrees).
// The list is sorted by angle, at whatever spacing things grew, so this is a
// pair of binary searches.
void window(const Planet& p, const Frame& f, int& i0, int& n) {
    int count = (int)p.harvest.size();
    i0 = 0;
    n = count;
    // A little past the edges: things have width.
    float span = f.halfSpanDeg + 80.0f / (p.baseRadius * DEG2RAD);
    if (count == 0 || span >= 179.0f) return;
    auto firstAtOrAfter = [&](float a) {
        int lo = 0, hi = count;
        while (lo < hi) {
            int mid = (lo + hi) / 2;
            if (p.harvest[mid].angle < a) lo = mid + 1;
            else hi = mid;
        }
        return lo;
    };
    float a0 = std::fmod(f.viewAngle - span + 720.0f, 360.0f);
    float a1 = std::fmod(f.viewAngle + span + 720.0f, 360.0f);
    i0 = firstAtOrAfter(a0);
    int i1 = firstAtOrAfter(a1);
    n = a0 <= a1 ? i1 - i0 : count - i0 + i1;
    if (i0 >= count) i0 = 0;
}

struct Rgb { float r, g, b; };
Rgb rgb(const float c[3]) { return Rgb{c[0], c[1], c[2]}; }
Rgb mixc(Rgb a, Rgb b, float t) { return Rgb{a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t}; }
Rgb mul(Rgb a, float k) { return Rgb{a.r * k, a.g * k, a.b * k}; }

}  // namespace

float groundDrop(float depth) { return 13.0f + 0.64f * depth; }
float depthAtDrop(float drop) { return (drop - 13.0f) / 0.64f; }
float scale(float depth) { return 1.0f + depth * 0.006f; }

Vec2 base(const Planet& p, const Planet::Harvestable& h) {
    float a = h.angle;
    float r = p.surfaceRadius(a) - groundDrop(h.depth);
    return Vec2(p.center.x + cosd(a) * r, p.center.y + sind(a) * r);
}

Vec2 center(const Planet& p, const Planet::Harvestable& h) {
    float a = h.angle;
    float r = p.surfaceRadius(a) - groundDrop(h.depth) + h.size * scale(h.depth) * 0.5f;
    return Vec2(p.center.x + cosd(a) * r, p.center.y + sind(a) * r);
}

float radius(const Planet::Harvestable& h) { return h.size * scale(h.depth) * 0.55f; }

Yield yield(const Planet::Harvestable& h) {
    switch (h.kind) {
        case Planet::Harvestable::Grass:   return Yield{0, 1};
        case Planet::Harvestable::Bulb:    return Yield{0, h.size < 26.0f ? 2 : 3};
        case Planet::Harvestable::Rock:    return Yield{1, h.size < 11.0f ? 1 : 2};   // a stone / a boulder
        default:                           return Yield{2, h.size < 21.0f ? 1 : 2};
    }
}

float toughness(Kind kind) {
    switch (kind) {
        case Planet::Harvestable::Grass:   return 0.30f;
        case Planet::Harvestable::Bulb:    return 0.55f;
        case Planet::Harvestable::Rock:    return 0.90f;
        default:                           return 0.75f;
    }
}

bool alive(const Planet::Harvestable& h) { return h.regrowIn <= 0.0f && h.grow > 0.7f; }

const float* color(const Planet& p, Kind kind) {
    if (kind == Planet::Harvestable::Grass || kind == Planet::Harvestable::Bulb) return p.look.flora;
    if (kind == Planet::Harvestable::Rock) return p.look.tones[3];
    return p.look.mineral;
}

void harvest(Planet::Harvestable& h) {
    bool plant = h.kind == Planet::Harvestable::Grass || h.kind == Planet::Harvestable::Bulb;
    h.grow = 0.0f;
    h.hp = 1.0f;
    // Plants are back within a minute or so; rock and crystal take a few.
    h.regrowIn = plant ? 35.0f + var(h, 5.0f) * 35.0f : 120.0f + var(h, 5.0f) * 80.0f;
}

int uproot(Planet& p, const Frame& f, Vec2 world, float radius) {
    int count = (int)p.harvest.size();
    if (count == 0) return 0;
    float cutR = dist(world.x, world.y, p.center.x, p.center.y);
    float cutAngle = atan2d(world.y - p.center.y, world.x - p.center.x);
    int i0, n, removed = 0;
    window(p, f, i0, n);
    for (int k = 0; k < n; k++) {
        int i = ((i0 + k) % count + count) % count;
        Planet::Harvestable& h = p.harvest[i];
        if (h.regrowIn > 0.0f || h.grow <= 0.01f) continue;
        float surf = p.surfaceRadius(h.angle);
        if (cutR + radius < surf - 2.0f) continue;        // bite is wholly underground here
        if (fabsf(h.depth) >= radius) continue;           // stands in front of / behind the hole
        // The bite's footprint on the ground is a circle in (along, depth).
        float along = wrapDeg(h.angle - cutAngle) * DEG2RAD * surf;
        if (along * along + h.depth * h.depth > radius * radius) continue;
        harvest(h);
        removed++;
    }
    return removed;
}

void update(Planet& p, float dt, int cutting) {
    for (int i = 0; i < (int)p.harvest.size(); i++) {
        Planet::Harvestable& h = p.harvest[i];
        if (h.regrowIn > 0.0f) {
            h.regrowIn -= dt;
            if (h.regrowIn < 0.0f) h.regrowIn = 0.0f;
        } else if (h.grow < 1.0f) {
            h.grow += dt / 5.0f;            // five seconds to grow back in
            if (h.grow > 1.0f) h.grow = 1.0f;
        } else if (h.hp < 1.0f && i != cutting) {
            h.hp += dt * 0.8f;              // a half-cut one recovers if left alone
            if (h.hp > 1.0f) h.hp = 1.0f;
        }
    }
}

int pick(const Planet& p, const Frame& f, Vec2 cursor, Vec2 from, float range, float slack) {
    int count = (int)p.harvest.size();
    if (count == 0) return -1;
    int i0, n;
    window(p, f, i0, n);
    int best = -1;
    float bestScore = 1e30f;
    for (int k = 0; k < n; k++) {
        int i = ((i0 + k) % count + count) % count;
        const Planet::Harvestable& h = p.harvest[i];
        if (!alive(h)) continue;
        Vec2 c = center(p, h);
        float rad = radius(h) + slack;
        float dc = dist(cursor.x, cursor.y, c.x, c.y);
        if (dc > rad) continue;
        if (dist(from.x, from.y, c.x, c.y) > range) continue;
        float score = dc / rad;
        if (score < bestScore) { bestScore = score; best = i; }
    }
    return best;
}

void draw(Renderer2D& r, const Planet& p, const Frame& f, bool front, int hot) {
    int count = (int)p.harvest.size();
    if (count == 0 || f.strength <= 0.01f) return;
    int i0, n;
    window(p, f, i0, n);

    const Rgb haze = rgb(p.look.haze);
    const Rgb ink{0.10f, 0.11f, 0.16f};
    const Rgb flora = rgb(p.look.flora), mineral = rgb(p.look.mineral);
    const Rgb rockLight = rgb(p.look.tones[3]), rockDark = rgb(p.look.tones[1]);
    const Rgb white{1.0f, 0.93f, 0.78f};

    // Farthest first, so nearer things overlap the ones behind them (a patch
    // of grass is a dozen tufts at slightly different depths).
    static std::vector<int> order;
    order.clear();
    for (int k = 0; k < n; k++) {
        int i = ((i0 + k) % count + count) % count;
        const Planet::Harvestable& h = p.harvest[i];
        if ((h.depth >= 0.0f) != front) continue;
        if (h.regrowIn > 0.0f || h.grow <= 0.01f) continue;
        order.push_back(i);
    }
    std::sort(order.begin(), order.end(), [&](int x, int y) {
        float dx = p.harvest[x].depth, dy = p.harvest[y].depth;
        return dx != dy ? dx < dy : x < y;
    });
    {
        for (int i : order) {
            const Planet::Harvestable& h = p.harvest[i];

            float a = h.angle;
            // Day/dusk/night light like everything else on the ground, but
            // lifted at night so you can still find something to shoot at.
            float lum = (f.light[0] + f.light[1] + f.light[2]) * (1.0f / 3.0f);
            float lift = lum < 0.42f && lum > 1e-4f ? 0.42f / lum : 1.0f;
            const Rgb light{f.light[0] * lift, f.light[1] * lift, f.light[2] * lift};
            // Depth tone: hazier behind the walking line, darker in front.
            float back = h.depth < 0 ? (-h.depth / 18.0f) * 0.24f : 0.0f;
            float fore = h.depth > 0 ? (h.depth / 55.0f) * 0.30f : 0.0f;
            float heat = i == hot ? 1.0f - h.hp : 0.0f;
            auto tone = [&](Rgb c) {
                c = mixc(c, haze, back);
                c = mixc(c, ink, fore);
                c = Rgb{c.r * light.r, c.g * light.g, c.b * light.b};
                return mixc(c, white, heat * 0.75f);
            };
            auto fill = [&](Rgb c) { Rgb t = tone(c); r.fill(t.r, t.g, t.b, f.strength); };

            bool plant = h.kind == Planet::Harvestable::Grass || h.kind == Planet::Harvestable::Bulb;
            float grown = smooth(0.0f, 1.0f, h.grow);
            float S = h.size * grown * (1.0f - 0.25f * heat);
            float sway = plant ? sinf(f.time * 0.6f + h.phase) : 0.0f;
            // Under the beam it shudders.
            float shake = heat > 0.0f ? sinf(f.time * 9.0f + h.phase * 3.0f) * heat * 1.2f : 0.0f;

            Vec2 b0 = base(p, h);
            r.pushMatrix();
            r.translate(b0.x, b0.y);
            r.rotate(a + 90.0f);            // +Y toward the planet; things grow along -Y
            r.scale(scale(h.depth));
            r.translate(shake, 0.0f);

            switch (h.kind) {
            case Planet::Harvestable::Grass: {
                // A fan of blades, alternating a dark and a light tone.
                const float spread[7] = {-34.0f, -21.0f, -9.0f, 2.0f, 12.0f, 24.0f, 36.0f};
                for (int bl = 0; bl < 7; bl++) {
                    float ang = spread[bl] + sway * (3.0f + fabsf(spread[bl]) * 0.12f);
                    float len = S * (0.60f + 0.40f * var(h, (float)bl));
                    float w = S * 0.085f;
                    fill((bl & 1) ? flora : mul(flora, 0.74f));
                    r.triangle(-w + spread[bl] * 0.012f * S, 0, w + spread[bl] * 0.012f * S, 0,
                               sind(ang) * len + spread[bl] * 0.012f * S, -cosd(ang) * len);
                }
                break;
            }
            case Planet::Harvestable::Bulb: {
                // A stem with two leaves and a bright bulb in the crystal color.
                float lean = sway * 0.05f * S + (var(h, 1.0f) - 0.5f) * 0.18f * S;
                float topY = -0.78f * S;
                fill(mul(flora, 0.72f));
                r.line(0, 0, lean, topY, S * 0.075f);
                fill(flora);
                float l1 = 0.30f, l2 = 0.52f;
                r.triangle(lean * l1, topY * l1, lean * l1 - 0.34f * S, topY * l1 - 0.10f * S,
                           lean * l1 - 0.10f * S, topY * l1 - 0.24f * S);
                r.triangle(lean * l2, topY * l2, lean * l2 + 0.32f * S, topY * l2 - 0.12f * S,
                           lean * l2 + 0.09f * S, topY * l2 - 0.24f * S);
                fill(mineral);
                r.circle(lean, topY - 0.06f * S, S * 0.17f, 16);
                fill(mixc(mineral, Rgb{1, 1, 1}, 0.55f));
                r.circle(lean - 0.05f * S, topY - 0.11f * S, S * 0.06f, 10);
                break;
            }
            case Planet::Harvestable::Crystal: {
                // Three leaning shards, each with a light and a dark face.
                struct Shard { float x, h, w, tilt; };
                const Shard shards[3] = {
                    {-0.20f, 0.62f + 0.12f * var(h, 1.0f), 0.26f, -0.30f},
                    { 0.22f, 0.70f + 0.14f * var(h, 2.0f), 0.26f,  0.30f},
                    { 0.00f, 1.00f,                         0.32f,  0.05f},
                };
                for (const Shard& sh : shards) {
                    float x = sh.x * S, hh = sh.h * S, w = sh.w * S * 0.5f;
                    float sx = sh.tilt * hh * 0.78f, tx = sh.tilt * hh;
                    float y1 = -hh * 0.78f;
                    fill(mul(mineral, 0.62f));       // shaded face
                    r.quad(x - w, 0, x, 0, x + sx, y1, x - w + sx, y1);
                    r.triangle(x - w + sx, y1, x + sx, y1, x + tx, -hh);
                    fill(mineral);                   // lit face
                    r.quad(x, 0, x + w, 0, x + w + sx, y1, x + sx, y1);
                    r.triangle(x + sx, y1, x + w + sx, y1, x + tx, -hh);
                }
                break;
            }
            default: {
                // A boulder: a lumpy dome in the terrain's own rock tones, with
                // a lighter top facet and a couple of ore flecks.
                Vec2 pts[8];
                const int N = 8;
                for (int v = 0; v < N; v++) {
                    float t = 180.0f - 180.0f * (float)v / (N - 1);
                    float k = 0.82f + 0.18f * var(h, (float)v + 3.0f);
                    pts[v] = Vec2(cosd(t) * S * 0.78f * k, -sind(t) * S * k);
                }
                fill(rockDark);
                r.fanPolygon(0, 0, pts, N, false);
                Vec2 top[5];
                for (int v = 0; v < 5; v++) {
                    Vec2 q = pts[v + 1];
                    top[v] = Vec2(q.x * 0.72f - S * 0.08f, q.y * 0.80f - S * 0.10f);
                }
                fill(rockLight);
                r.fanPolygon(-S * 0.08f, -S * 0.46f, top, 5, false);
                fill(mineral);
                r.triangle(S * 0.18f, -S * 0.20f, S * 0.34f, -S * 0.30f, S * 0.24f, -S * 0.44f);
                r.triangle(-S * 0.40f, -S * 0.12f, -S * 0.28f, -S * 0.26f, -S * 0.22f, -S * 0.10f);
                break;
            }
            }
            r.popMatrix();
        }
    }
}

}  // namespace NearField
