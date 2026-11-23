#include "deposits.h"
#include <algorithm>
#include <cmath>

namespace Deposits {

namespace {

using Ore = Planet::Ore;
using Kind = Planet::Ore::Kind;

struct Rgb { float r, g, b; };
Rgb rgb(const float c[3]) { return Rgb{c[0], c[1], c[2]}; }
Rgb mixc(Rgb a, Rgb b, float t) { return Rgb{a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t}; }
Rgb mul(Rgb a, float k) { return Rgb{a.r * k, a.g * k, a.b * k}; }

const Rgb GOLD{1.00f, 0.80f, 0.26f};        // as the caves' gold veins (cavefield.cpp)
const Rgb GOLD_DARK{0.74f, 0.50f, 0.12f};
const Rgb GOLD_PALE{1.00f, 0.93f, 0.62f};   // a nugget's bright facet
const Rgb IRON{0.86f, 0.81f, 0.74f};        // ferrite: a pale metal face...
const Rgb IRON_MID{0.61f, 0.53f, 0.49f};    // ...a mid one...
const Rgb RUST{0.50f, 0.29f, 0.21f};        // ...and a rust-red shaded one

float smooth(float e0, float e1, float x) {
    float t = (x - e0) / (e1 - e0);
    t = t < 0 ? 0 : (t > 1 ? 1 : t);
    return t * t * (3.0f - 2.0f * t);
}

// Cheap stable 0..1 values per clump (shape variation), from its phase.
float var(const Ore& o, float salt) {
    float v = sinf(o.phase * 12.9898f + salt * 78.233f) * 43758.5453f;
    return v - floorf(v);
}

// ---- The seeding grid ------------------------------------------------------

unsigned int mix32(unsigned int h) {
    h ^= h >> 16; h *= 0x7FEB352Du;
    h ^= h >> 15; h *= 0x846CA68Bu;
    h ^= h >> 16;
    return h;
}
unsigned int hash3(unsigned int a, unsigned int b, unsigned int c) {
    return mix32(a * 0x9E3779B1u + mix32(b * 0x85EBCA77u + mix32(c * 0xC2B2AE3Du + 0x27D4EB2Fu)));
}
struct Rng {
    unsigned int s;
    explicit Rng(unsigned int seed) : s(seed ? seed : 0x9E3779B9u) {}
    float next01() {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return (s & 0xFFFFFFu) * (1.0f / 16777216.0f);
    }
    float range(float a, float b) { return a + (b - a) * next01(); }
};
// Smooth 0..1 value noise along a line.
float noise1(unsigned int seed, float x) {
    float fl = floorf(x), f = x - fl;
    unsigned int k = (unsigned int)(int)fl;
    float a = (mix32(seed ^ (k * 0x632BE5ABu)) & 0xFFFFFFu) * (1.0f / 16777216.0f);
    float b = (mix32(seed ^ ((k + 1u) * 0x632BE5ABu)) & 0xFFFFFFu) * (1.0f / 16777216.0f);
    float u = f * f * (3.0f - 2.0f * f);
    return a + (b - a) * u;
}

constexpr int MAX_VEIN = 5;            // clumps in one vein (one grid square)
constexpr float MAX_SIZE = 14.0f;      // the largest clump radius there is (for searches)
constexpr float MIN_COVER = 8.0f;      // rock over a clump's top, at least
constexpr float MAX_DEPTH = 3000.0f;   // nothing deeper than this
// A square's chance of holding a vein, in ordinary ground at the default
// richness. Tuned so that tunnelling turns up a find every ten seconds or so
// (see docs/architecture/world.md).
constexpr float BASE_CHANCE = 0.58f;

// One clump of the grid, before anything is known about it.
struct Clump {
    Vec2 local;
    float size, phase;
    Kind kind;
    unsigned long long id;
};

unsigned long long clumpId(int cx, int cy, int k) {
    return ((unsigned long long)((unsigned int)(cx + 32768) & 0xFFFFu) << 24) |
           ((unsigned long long)((unsigned int)(cy + 32768) & 0xFFFFu) << 8) | (unsigned long long)(k & 0xFF);
}

float depthAt(const Planet& p, Vec2 local) {
    return p.surfaceRadius(atan2d(local.y, local.x)) - local.length();
}

// How likely each kind is at a depth, on this kind of world: ferrite from a
// little way down (thinning out deep), the world's crystal from ~80, gold
// from ~150 and more of it the deeper you go.
void kindWeights(const Planet& p, float d, float w[3]) {
    static const float bias[7][3] = {
        {1.0f, 1.0f, 1.0f},   // temperate
        {1.2f, 0.9f, 1.3f},   // desert
        {0.8f, 1.8f, 0.6f},   // ice
        {1.0f, 0.9f, 1.9f},   // volcanic
        {1.0f, 1.2f, 0.8f},   // jungle
        {0.8f, 1.7f, 1.0f},   // exotic
        {1.6f, 0.9f, 1.2f},   // barren
    };
    int a = p.archetype >= 0 && p.archetype < 7 ? p.archetype : 0;
    w[0] = (1.0f - 0.75f * smooth(260.0f, 650.0f, d)) * bias[a][0];
    w[1] = 0.55f * smooth(80.0f, 220.0f, d) * bias[a][1];
    w[2] = 0.70f * smooth(140.0f, 420.0f, d) * bias[a][2];
}

// What one square of the grid holds: a short vein of clumps, or nothing.
// Follows from the planet's seed and the square alone.
int cell(const Planet& p, int cx, int cy, Clump out[MAX_VEIN]) {
    Vec2 mid((cx + 0.5f) * CELL, (cy + 0.5f) * CELL);
    float r = mid.length();
    if (r < 1.0f) return 0;
    float ang = atan2d(mid.y, mid.x);
    float d = p.surfaceRadius(ang) - r;
    if (d < -0.5f * CELL || d > MAX_DEPTH) return 0;

    Rng rng(hash3(p.seed, (unsigned int)cx, (unsigned int)cy));
    // Rich and barren stretches along the ground, a few hundred strides
    // each, and a little richer deeper down.
    float along = ang * DEG2RAD * p.baseRadius;
    float n = noise1(p.seed * 7919u + 17u, along / 1500.0f);
    float regional = 0.2f + 1.6f * n * sqrtf(n);
    float deep = 1.0f + 0.2f * smooth(150.0f, 700.0f, d);
    float chance = BASE_CHANCE * gDebug.oreRichness * regional * deep;
    if (rng.next01() >= std::min(chance, 0.9f)) return 0;

    float w[3];
    kindWeights(p, std::max(d, 20.0f), w);
    float total = w[0] + w[1] + w[2];
    if (total <= 0.0f) return 0;
    float k0 = rng.next01() * total;
    Kind kind = k0 < w[0] ? Ore::Ferrite : (k0 < w[0] + w[1] ? Ore::Crystal : Ore::Gold);

    // A vein: a few clumps in a wandering line, roughly along the ground
    // (the way strata run), each a little apart from the last. (Its kind
    // comes from the square's depth; a clump that wanders up shallower than
    // its kind is ever found is left out.)
    int count = kind == Ore::Gold ? 1 + (int)(rng.next01() * 3.0f)
              : kind == Ore::Crystal ? 2 + (int)(rng.next01() * 3.0f)
              : 2 + (int)(rng.next01() * 4.0f);
    if (count > MAX_VEIN) count = MAX_VEIN;
    float lo = kind == Ore::Ferrite ? 7.0f : (kind == Ore::Crystal ? 6.0f : 5.0f);
    float hi = kind == Ore::Ferrite ? 12.0f : (kind == Ore::Crystal ? 10.0f : 8.5f);
    float grow = 1.0f + 0.15f * smooth(200.0f, 700.0f, d);
    float minDepth = kind == Ore::Gold ? 130.0f : (kind == Ore::Crystal ? 70.0f : 0.0f);
    float heading = ang + 90.0f + rng.range(-35.0f, 35.0f);
    Vec2 pos[MAX_VEIN];
    float size[MAX_VEIN], phase[MAX_VEIN];
    Vec2 at(0.0f, 0.0f);
    for (int k = 0; k < count; k++) {
        size[k] = rng.range(lo, hi) * grow;
        phase[k] = rng.range(0.0f, 6.2831853f);
        if (k > 0) {
            heading += rng.range(-25.0f, 25.0f);
            // Edge to edge a little more than a mined clump's pocket reaches
            // past it, and well inside what that pocket reveals: taking one
            // out shows the next without cutting into it.
            at += Vec2(cosd(heading), sind(heading)) * (size[k - 1] + size[k] + rng.range(POCKET + 0.5f, POCKET + REVEAL_GAP - 0.5f));
        }
        pos[k] = at;
    }
    // Center the vein in its square, give or take, and keep every clump
    // inside it by its own size and a little more: clumps of neighbouring
    // squares never touch.
    Vec2 mn = pos[0], mx = pos[0];
    for (int k = 1; k < count; k++) {
        mn.x = std::min(mn.x, pos[k].x); mn.y = std::min(mn.y, pos[k].y);
        mx.x = std::max(mx.x, pos[k].x); mx.y = std::max(mx.y, pos[k].y);
    }
    float room = 0.5f * CELL - MAX_SIZE - 2.0f;
    float jx = std::max(0.0f, room - 0.5f * (mx.x - mn.x)), jy = std::max(0.0f, room - 0.5f * (mx.y - mn.y));
    Vec2 shift = mid - (mn + mx) * 0.5f + Vec2(rng.range(-jx, jx), rng.range(-jy, jy));
    int found = 0;
    for (int k = 0; k < count; k++) {
        Vec2 c = pos[k] + shift;
        float lim = 0.5f * CELL - size[k] - 2.0f;
        if (fabsf(c.x - mid.x) > lim || fabsf(c.y - mid.y) > lim) continue;
        float dk = depthAt(p, c);
        if (dk - size[k] < MIN_COVER || dk < minDepth || dk > MAX_DEPTH) continue;
        out[found++] = Clump{c, size[k], phase[k], kind, clumpId(cx, cy, k)};
    }
    return found;
}

// Every grid clump within `reach` of a planet-local point (by square).
template <class F>
void forClumps(const Planet& p, Vec2 local, float reach, F&& f) {
    int x0 = (int)floorf((local.x - reach) / CELL), x1 = (int)floorf((local.x + reach) / CELL);
    int y0 = (int)floorf((local.y - reach) / CELL), y1 = (int)floorf((local.y + reach) / CELL);
    Clump c[MAX_VEIN];
    for (int cy = y0; cy <= y1; cy++)
        for (int cx = x0; cx <= x1; cx++) {
            int n = cell(p, cx, cy, c);
            for (int k = 0; k < n; k++) f(c[k]);
        }
}

// What is known of a grid clump: -2 = not looked at yet, -1 = hollowed out,
// else its index in p.ores.
int seen(const Planet& p, unsigned long long id) {
    auto it = p.oreSeen.find(id);
    return it == p.oreSeen.end() ? -2 : it->second;
}

// A clump whose middle is open space was hollowed out by a cave (or dug
// through some other way): there is nothing there.
bool hollow(const Planet& p, const Clump& c) {
    return p.carve.ready() && p.carve.sample(c.local) < -0.4f * c.size;
}

// Look at a clump for the first time: note it as hollowed out, or reveal it.
// Returns its index in p.ores, or -1.
int reveal(Planet& p, const Clump& c) {
    if (hollow(p, c)) {
        p.oreSeen[c.id] = -1;
        return -1;
    }
    Ore o{};
    o.local = c.local;
    o.size = c.size;
    o.phase = c.phase;
    o.kind = c.kind;
    o.mined = false;
    o.hp = 1.0f;
    o.heat = 0.0f;
    o.found = 0.0f;
    o.id = c.id;
    p.ores.push_back(o);
    int idx = (int)p.ores.size() - 1;
    p.oreSeen[c.id] = idx;
    return idx;
}

// ---- What a clump is made of ------------------------------------------------
// As many pieces as it yields, laid out in the clump's own frame: origin at
// its middle, -Y up (away from the planet), world units. In the order the
// laser takes them: the small ones first, the biggest last.
constexpr int MAX_PIECES = 4;
struct Piece {
    Vec2 at;       // a nugget's or cube's middle; a shard's base
    float size;    // a nugget's or cube's radius; a shard's length
    float rot;     // degrees
};

int layout(const Ore& o, Piece out[MAX_PIECES]) {
    int n = yield(o).amount;
    n = n < 1 ? 1 : (n > MAX_PIECES ? MAX_PIECES : n);
    float L = o.size;
    if (o.kind == Ore::Crystal) {
        // A fan of shards from one base low in the lump, the middle ones
        // longest. The outer ones go first.
        int slot[MAX_PIECES];
        for (int k = 0, lo = 0, hi = n - 1; k < n; k++) slot[k] = (k & 1) ? hi-- : lo++;
        for (int k = 0; k < n; k++) {
            float t = n > 1 ? (float)slot[k] / (n - 1) : 0.5f;          // 0..1 across the fan
            float tilt = (t - 0.5f) * 82.0f + (var(o, 31.0f + slot[k]) - 0.5f) * 12.0f;
            float len = L * (1.50f - 0.62f * fabsf(t - 0.5f) * 2.0f) * (0.90f + 0.2f * var(o, 40.0f + slot[k]));
            out[k] = Piece{Vec2((t - 0.5f) * L * 0.62f, L * 0.42f), len, tilt};
        }
        return n;
    }
    // Nuggets / cubes: the biggest a little off the middle, the smaller ones
    // crowded against one side of it.
    static const float goldSize[3][MAX_PIECES] = {{0.58f}, {0.42f, 0.56f}, {0.34f, 0.42f, 0.54f}};
    static const float ironSize[4][MAX_PIECES] = {{0.52f}, {0.38f, 0.50f}, {0.31f, 0.38f, 0.48f}, {0.27f, 0.32f, 0.38f, 0.46f}};
    bool gold = o.kind == Ore::Gold;
    if (gold && n > 3) n = 3;
    const float* sz = gold ? goldSize[n - 1] : ironSize[n - 1];
    float a0 = o.phase * 57.3f;
    int big = n - 1, m = n - 1;
    out[big] = Piece{Vec2(cosd(a0), sind(a0)) * (m > 0 ? L * 0.20f : 0.0f), L * sz[big], (var(o, 25.0f) - 0.5f) * 44.0f};
    for (int j = 0; j < m; j++) {
        float a = a0 + 180.0f + (j - 0.5f * (m - 1)) * (m >= 3 ? 78.0f : 104.0f) + (var(o, 10.0f + j) - 0.5f) * 22.0f;
        float rr = (out[big].size + L * sz[j]) * (gold ? 0.74f : 0.82f);
        out[j] = Piece{out[big].at + Vec2(cosd(a) * rr, sind(a) * rr), L * sz[j], (var(o, 26.0f + j) - 0.5f) * 44.0f};
    }
    return n;
}

// A piece's middle (where the beam works it, where its bits come from).
Vec2 pieceMiddle(const Ore& o, const Piece& q) {
    if (o.kind != Ore::Crystal) return q.at;
    return q.at + Vec2(sind(q.rot), -cosd(q.rot)) * (q.size * 0.5f);
}

// The clump's own frame, as drawn: turned so -Y is away from the planet.
float frameAngle(const Ore& o) { return atan2d(o.local.y, o.local.x) + 90.0f; }

}  // namespace

Vec2 center(const Planet& p, const Ore& o) {
    float len = o.local.length();
    Vec2 up = len > 1e-3f ? o.local * (1.0f / len) : Vec2(0, 1);
    return p.center + o.local - up * NearField::WALK_DROP;
}

float radius(const Ore& o) { return o.size * 1.05f; }

NearField::Yield yield(const Ore& o) {
    switch (o.kind) {
        case Ore::Ferrite: return NearField::Yield{1, o.size > 10.5f ? 4 : (o.size > 8.5f ? 3 : 2)};
        case Ore::Crystal: return NearField::Yield{2, o.size > 8.5f ? 4 : 3};
        default:           return NearField::Yield{5, o.size > 7.0f ? 3 : 2};
    }
}

float toughness(Kind kind) {
    switch (kind) {
        case Ore::Ferrite: return 0.90f;
        case Ore::Crystal: return 1.00f;
        default:           return 1.30f;
    }
}

void color(const Planet& p, Kind kind, float out[3]) {
    Rgb c = kind == Ore::Ferrite ? IRON : (kind == Ore::Crystal ? rgb(p.look.mineral) : GOLD);
    out[0] = c.r; out[1] = c.g; out[2] = c.b;
}

int pieces(const Ore& o) {
    Piece q[MAX_PIECES];
    return layout(o, q);
}

int taken(const Ore& o) {
    int n = pieces(o);
    if (o.hp <= 0.0f) return n;
    int k = (int)floorf((1.0f - o.hp) * n + 1e-4f);
    return k < 0 ? 0 : (k > n - 1 ? n - 1 : k);      // the last goes with the clump
}

Vec2 pieceCenter(const Planet& p, const Ore& o, int k) {
    Piece q[MAX_PIECES];
    int n = layout(o, q);
    k = k < 0 ? 0 : (k > n - 1 ? n - 1 : k);
    return center(p, o) + Mat3::rotation(frameAngle(o)).transform(pieceMiddle(o, q[k]));
}

float burnSeconds(Kind kind) {
    float k = gDebug.oreBurn > 0.05f ? gDebug.oreBurn : 0.05f;
    return toughness(kind) * k;
}

int blocking(Planet& p, Vec2 world, float rad) {
    Vec2 local = world - p.center;
    int hit = -1;
    float best = BLOCK_OVERLAP;
    forClumps(p, local, rad + MAX_SIZE, [&](const Clump& c) {
        float overlap = rad + c.size - (c.local - local).length();
        if (overlap <= best) return;
        int s = seen(p, c.id);
        if (s == -1) return;                                 // hollowed out
        if (s >= 0 && p.ores[s].mined) return;              // already taken out
        if (s == -2 && (s = reveal(p, c)) < 0) return;
        best = overlap;
        hit = s;
    });
    return hit;
}

int revealNear(Planet& p, Vec2 world, float rad) {
    Vec2 local = world - p.center;
    int found = 0;
    forClumps(p, local, rad + MAX_SIZE + REVEAL_GAP, [&](const Clump& c) {
        float gap = (c.local - local).length() - rad - c.size;
        if (gap >= REVEAL_GAP || seen(p, c.id) != -2) return;
        if (reveal(p, c) >= 0) found++;
    });
    return found;
}

float glitter(const Planet& p, Vec2 world, float rad, Kind* kind) {
    Vec2 local = world - p.center;
    float best = 0.0f;
    forClumps(p, local, rad + MAX_SIZE + GLITTER_RANGE, [&](const Clump& c) {
        float gap = (c.local - local).length() - rad - c.size;
        if (gap < REVEAL_GAP || gap > GLITTER_RANGE) return;   // (any closer is revealed)
        if (seen(p, c.id) != -2 || hollow(p, c)) return;
        float k = 1.0f - (gap - REVEAL_GAP) / (GLITTER_RANGE - REVEAL_GAP);
        if (k > best) {
            best = k;
            if (kind) *kind = c.kind;
        }
    });
    return best;
}

int scanWindow(Planet& p) {
    if (!p.carve.ready()) return 0;
    Vec2 mid = p.carve.center();
    float half = 0.5f * (CarveMap::N - 1) * CarveMap::CELL;
    int found = 0;
    forClumps(p, mid, half, [&](const Clump& c) {
        if (seen(p, c.id) != -2 || !p.carve.contains(c.local, c.size + REVEAL_GAP + 2.0f)) return;
        float s = p.carve.sample(c.local);       // its middle's distance to dug space
        if (s < -0.4f * c.size) p.oreSeen[c.id] = -1;
        else if (s < c.size + REVEAL_GAP && reveal(p, c) >= 0) found++;
    });
    return found;
}

int pick(const Planet& p, const CaveField::Frame& f, Vec2 cursor, Vec2 from, Vec2 eye, float range, float slack) {
    if (f.inside < 0.5f) return -1;      // only seen from underground
    int best = -1;
    float bestScore = 1e30f;
    for (int i = 0; i < (int)p.ores.size(); i++) {
        const Ore& o = p.ores[i];
        if (o.mined) continue;
        Vec2 c = center(p, o);
        float rad = radius(o) + slack;
        float dc = dist(cursor.x, cursor.y, c.x, c.y);
        if (dc > rad) continue;
        if (dist(from.x, from.y, c.x, c.y) > range) continue;
        float score = dc / rad;
        if (score >= bestScore) continue;
        // A clear line from the tool to the clump's face (its middle is in
        // the rock): no mining through a wall.
        Vec2 mid = p.center + o.local;
        Vec2 toEye = eye - mid;
        float le = toEye.length();
        Vec2 target = le > o.size + 2.0f ? mid + toEye * ((o.size + 1.5f) / le) : mid;
        Vec2 d = target - eye;
        float len = d.length();
        bool blocked = false;
        for (float t = 6.0f; t < len - 4.0f; t += 5.0f) {
            if (p.rockAt(eye + d * (t / len)) > 1.5f) { blocked = true; break; }
        }
        if (blocked) continue;
        bestScore = score;
        best = i;
    }
    return best;
}

void update(Planet& p, float dt, int burning) {
    for (int i = 0; i < (int)p.ores.size(); i++) {
        Ore& o = p.ores[i];
        if (o.found < 100.0f) o.found += dt;
        // Out from under the cutter it cools: from red-hot in a couple of
        // seconds. (What the laser has taken does not come back.)
        if (!o.mined && o.heat > 0.0f && i != burning) {
            o.heat -= dt * 0.45f;
            if (o.heat < 0.0f) o.heat = 0.0f;
        }
    }
}

CarveMap::Rect extract(Planet& p, int idx) {
    if (idx < 0 || idx >= (int)p.ores.size() || p.ores[idx].mined) return CarveMap::Rect{};
    p.ores[idx].mined = true;
    p.ores[idx].hp = 0.0f;
    Vec2 world = p.center + p.ores[idx].local;
    float r = p.ores[idx].size + POCKET;
    CarveMap::Rect rc = p.dig(world, r);
    revealNear(p, world, r);
    return rc;
}

int scan(const Planet& p, Vec2 world, float range, ScanHit* out, int maxCount) {
    Vec2 local = world - p.center;
    int n = 0;
    forClumps(p, local, range, [&](const Clump& c) {
        if (n >= maxCount || (c.local - local).length() > range) return;
        int s = seen(p, c.id);
        if (s == -1 || (s == -2 && hollow(p, c))) return;
        if (s >= 0 && p.ores[s].mined) return;
        Ore o{};
        o.local = c.local;
        out[n++] = ScanHit{center(p, o), c.kind, c.size};
    });
    return n;
}

int revealAll(Planet& p, Vec2 world, float range) {
    Vec2 local = world - p.center;
    int found = 0;
    forClumps(p, local, range, [&](const Clump& c) {
        if ((c.local - local).length() > range || seen(p, c.id) != -2) return;
        if (reveal(p, c) >= 0) found++;
    });
    return found;
}

void draw(Renderer2D& r, const Planet& p, const CaveField::Frame& f, int hot, int burning) {
    if (p.ores.empty() || f.strength <= 0.01f) return;
    float alpha = smooth(0.15f, 0.85f, f.inside) * f.strength;
    if (alpha <= 0.01f) return;

    // The rock a clump sits in is drawn in the ground's own tones at any
    // depth, in this hour's light — lifted a little at night, as for the
    // plants, so a find can still be seen.
    float lum = (f.light[0] + f.light[1] + f.light[2]) * (1.0f / 3.0f);
    float lift = lum < 0.42f && lum > 1e-4f ? 0.42f / lum : 1.0f;
    const Rgb day{f.light[0] * lift, f.light[1] * lift, f.light[2] * lift};
    const Rgb white{1.0f, 0.95f, 0.85f};
    const Rgb mineral = rgb(p.look.mineral);
    float reach2 = (f.reach + 60.0f) * (f.reach + 60.0f);

    // One smooth glow, bright at the middle and nothing at the edge.
    auto halo = [&](Vec2 c, float rad, Rgb g, float a0) {
        Renderer2D::Color in{g.r, g.g, g.b, a0}, edge{g.r, g.g, g.b, 0.0f};
        const int SEG = 20;
        for (int k = 0; k < SEG; k++) {
            float a1 = 360.0f * k / SEG, a2 = 360.0f * (k + 1) / SEG;
            r.triangleV(c, in, Vec2(c.x + cosd(a1) * rad, c.y + sind(a1) * rad), edge,
                        Vec2(c.x + cosd(a2) * rad, c.y + sind(a2) * rad), edge);
        }
    };

    // Three sweeps: the glows (additive, under every shape) — a crystal's
    // halo, gold's faint one, the red of a clump the cutter has heated —
    // then the clumps themselves, then the glint on each (additive again).
    for (int sweep = 0; sweep < 3; sweep++) {
        r.setBlend(sweep == 1 ? Renderer2D::Blend::Normal : Renderer2D::Blend::Additive);
        for (int i = 0; i < (int)p.ores.size(); i++) {
            const Ore& o = p.ores[i];
            if (o.mined) continue;
            Vec2 c = center(p, o);
            float dx = c.x - f.focus.x, dy = c.y - f.focus.y;
            if (dx * dx + dy * dy > reach2) continue;
            float L = o.size;
            Piece pc[MAX_PIECES];
            int n = layout(o, pc);
            int gone = taken(o);
            float heat = o.heat;
            // Red through most of the burn, orange to yellow as it goes.
            const Rgb hotC = mixc(Rgb{0.92f, 0.13f, 0.05f}, Rgb{1.0f, 0.70f, 0.26f}, smooth(0.62f, 1.0f, heat));

            if (sweep == 0) {
                float pulse = 0.88f + 0.12f * sinf(f.time * 0.5f + o.phase);
                float left = 0.35f + 0.65f * (float)(n - gone) / n;         // fewer pieces, less light
                if (o.kind == Ore::Crystal) halo(c, 2.2f * L, mineral, 0.10f * alpha * pulse * left);
                else if (o.kind == Ore::Gold) halo(c, 2.0f * L, GOLD, 0.055f * alpha * pulse * left);
                if (heat > 0.01f) {
                    float fl = i == burning ? 0.86f + 0.14f * sinf(f.time * 7.0f + o.phase) : 1.0f;
                    halo(c, L * (1.5f + 1.2f * heat), hotC, 0.40f * smooth(0.0f, 0.8f, heat) * alpha * fl);
                }
                continue;
            }

            r.pushMatrix();
            r.translate(c.x, c.y);
            r.rotate(frameAngle(o));                 // -Y is up, away from the planet

            if (sweep == 2) {
                // A four-point glint on one piece at a time, every second and
                // a half or so. (Not on something glowing red.)
                float cyc = f.time * 0.22f + o.phase * 1.7f;
                float ft = cyc - floorf(cyc);
                if (ft < 0.30f && gone < n && heat < 0.25f) {
                    int k = gone + (int)(var(o, 70.0f + floorf(cyc)) * (n - gone) * 0.999f);
                    float s = sinf(ft * (3.14159f / 0.30f));
                    Vec2 at = pieceMiddle(o, pc[k]);
                    if (o.kind != Ore::Crystal) at = at + Vec2(-0.26f, -0.30f) * pc[k].size;   // on its lit facet
                    float a = L * (0.30f + 0.34f * s), b = a * 0.13f;
                    r.fill(1.0f, 0.97f, 0.88f, 0.85f * s * alpha * (1.0f - heat * 4.0f));
                    r.quad(at.x - a, at.y, at.x, at.y - b, at.x + a, at.y, at.x, at.y + b);
                    r.quad(at.x, at.y - a, at.x + b, at.y, at.x, at.y + a, at.x - b, at.y);
                }
                r.popMatrix();
                continue;
            }

            float flash = o.found < 1.0f ? 1.0f - o.found : 0.0f;   // just found: it all glints
            float g[3];
            p.groundTone(c, g);
            const Rgb rock{g[0] * day.r, g[1] * day.g, g[2] * day.b};
            // The cutter's heat takes every tone toward the glow, lighter
            // tones to a brighter red, so the facets still read: the ore
            // glows in a lump of dull red rock.
            float hotK = smooth(0.0f, 0.62f, heat) * 0.94f;
            auto fill = [&](Rgb col, float whiten) {
                if (whiten > 0.0f) col = mixc(col, white, whiten);
                if (hotK > 0.0f) {
                    float v = (col.r + col.g + col.b) * (1.0f / 3.0f);
                    col = mixc(col, mul(hotC, 0.30f + 0.95f * (v > 1.0f ? 1.0f : v)), hotK);
                }
                r.fill(col.r, col.g, col.b, alpha);
            };
            // Under the cutter the whole clump shudders, harder as it heats.
            if (i == burning) r.translate(sinf(f.time * 9.0f + o.phase * 3.0f) * (0.3f + 0.9f * heat), 0.0f);
            // Under the mining laser only the piece being worked does, and
            // whitens until it comes loose.
            float work = 0.0f;
            if (i == hot && gone < n) {
                work = (1.0f - o.hp) * n - gone;
                work = work < 0.0f ? 0.0f : (work > 1.0f ? 1.0f : work);
            }

            // The matrix: a lump of rock a step darker than the rock around it
            // (still well clear of a hole's darker shade), with a lit facet
            // toward the top.
            const int NP = 9;
            Vec2 lump[NP], facet[NP];
            for (int v = 0; v < NP; v++) {
                float t = 360.0f * v / NP + o.phase * 57.3f;
                float k = 0.90f + 0.30f * var(o, (float)v + 1.0f);
                lump[v] = Vec2(cosd(t) * L * k, sind(t) * L * k * 0.92f);
                facet[v] = Vec2(lump[v].x * 0.62f - L * 0.12f, lump[v].y * 0.62f - L * 0.20f);
            }
            fill(mul(rock, 0.70f), 0.0f);
            r.fanPolygon(0.0f, 0.0f, lump, NP, true);
            fill(mul(rock, 0.84f), 0.0f);
            r.fanPolygon(-L * 0.12f, -L * 0.20f, facet, NP, true);

            // The pieces still in it, the last to go at the back.
            for (int k = n - 1; k >= gone; k--) {
                const Piece& q = pc[k];
                float w = k == gone ? work : 0.0f;
                float wob = w > 0.0f ? sinf(f.time * 12.0f + k * 2.1f) * w * 0.9f : 0.0f;
                float S = q.size;
                r.pushMatrix();
                r.translate(q.at.x + wob, q.at.y);
                if (o.kind == Ore::Crystal) {
                    // A shard: a shaded and a lit face, and a tip that
                    // catches the light (emitted light: no hour, no shade).
                    r.rotate(q.rot);
                    float hw = L * 0.21f, y1 = -S * 0.74f;
                    fill(mul(mineral, 0.58f), w * 0.8f);
                    r.quad(-hw, 0.0f, 0.0f, 0.0f, 0.0f, y1, -hw, y1);
                    r.triangle(-hw, y1, 0.0f, y1, 0.0f, -S);
                    fill(mixc(mineral, white, flash * 0.8f), w * 0.8f);
                    r.quad(0.0f, 0.0f, hw, 0.0f, hw, y1, 0.0f, y1);
                    fill(mixc(mineral, Rgb{1, 1, 1}, 0.50f + 0.4f * flash), w * 0.8f);
                    r.triangle(0.0f, y1, hw, y1, 0.0f, -S);
                } else if (o.kind == Ore::Gold) {
                    // A nugget: a rough lump of it, dark underneath, with a
                    // bright facet on top.
                    const int NV = 7;
                    Vec2 base[NV], body[NV], top[NV];
                    for (int v = 0; v < NV; v++) {
                        float t = 360.0f * v / NV + q.rot * 4.0f;
                        float kk = 0.82f + 0.28f * var(o, 60.0f + k * 9.0f + v);
                        base[v] = Vec2(cosd(t) * S * kk, sind(t) * S * kk * 0.90f);
                        body[v] = Vec2(base[v].x * 0.76f - S * 0.10f, base[v].y * 0.76f - S * 0.13f);
                        top[v] = Vec2(base[v].x * 0.38f - S * 0.26f, base[v].y * 0.38f - S * 0.30f);
                    }
                    fill(GOLD_DARK, w * 0.7f);
                    r.fanPolygon(0.0f, 0.0f, base, NV, true);
                    fill(mixc(GOLD, white, flash * 0.6f), w * 0.8f);
                    r.fanPolygon(-S * 0.10f, -S * 0.13f, body, NV, true);
                    fill(mixc(GOLD_PALE, Rgb{1, 1, 1}, flash * 0.8f), w * 0.8f);
                    r.fanPolygon(-S * 0.26f, -S * 0.30f, top, NV, true);
                } else {
                    // A cube of it, the way the metal crystallizes: a pale top,
                    // a mid face, a rust-dark one.
                    r.rotate(q.rot);
                    float hx = S * 0.87f, hy = S * 0.5f;
                    fill(RUST, w * 0.7f);
                    r.quad(0.0f, 0.0f, hx, -hy, hx, hy, 0.0f, S);
                    fill(IRON_MID, w * 0.8f);
                    r.quad(-hx, -hy, 0.0f, 0.0f, 0.0f, S, -hx, hy);
                    fill(mixc(IRON, Rgb{1, 1, 1}, flash * 0.8f), w * 0.8f);
                    r.quad(0.0f, -S, hx, -hy, 0.0f, 0.0f, -hx, -hy);
                }
                r.popMatrix();
            }
            if (o.kind == Ore::Crystal) {
                // The shards grow out of the lump: its own rock closes over
                // where they are rooted.
                const Vec2 lip[7] = {Vec2(-0.66f, 0.60f), Vec2(-0.40f, 0.40f), Vec2(0.06f, 0.33f), Vec2(0.48f, 0.42f),
                                     Vec2(0.68f, 0.62f), Vec2(0.28f, 0.72f), Vec2(-0.32f, 0.70f)};
                Vec2 pts[7];
                for (int v = 0; v < 7; v++) pts[v] = lip[v] * L;
                fill(mul(rock, 0.70f), 0.0f);
                r.fanPolygon(0.0f, L * 0.56f, pts, 7, true);
            }
            r.popMatrix();
        }
    }
    r.setBlend(Renderer2D::Blend::Normal);
}

}  // namespace Deposits
