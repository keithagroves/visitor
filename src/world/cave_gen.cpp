// Natural caves: Planet::buildCaves. One cave system for most mesas, cut as
// bites into the same fine layer the player digs (CarveMap) — so a cave
// collides, draws and cuts away exactly like a dug tunnel — and then furnished
// with formations and things to mine (Planet::caveProps).
//
// A system is laid out in the mesa's own frame: s = distance along the ground
// from the mesa's center, h = height above the (smoothed) ground the mesa
// stands on. Every passage and room is described by its FLOOR, and each bite
// stands on that floor (its center one radius above it), so ceilings can be as
// lumpy as they like while floors stay walkable.
//
//   mouth      a flared opening in a flank, at ground level
//   gallery    the passage in from a mouth, winding a little
//   hall       a tall room under the mesa's top; sometimes a skylight shaft
//              out through the top, sometimes a second gallery out the far
//              flank (a through-cave)
//   descent    a ramp down from the hall's free end — or, in a through-cave, a
//              well in the hall's floor — to
//   cavern     a big room well below ground level, sometimes entered from a
//              ledge partway up its wall
//   grotto     a small crystal-lined room off the cavern, through a low crawl
//   geode      a second, deeper room: the richest pickings
//
// Headroom: the astronaut is about 37 tall; nothing here is lower than 42.
#include "planet.h"
#include <algorithm>
#include <cmath>

namespace {

const float PI = 3.14159265f;

// Same generator as planet.cpp's (kept local: each cave seeds its own).
struct Rng {
    unsigned int state;
    explicit Rng(unsigned int s) : state(s ? s : 1) {}
    float next01() {
        state = state * 1664525u + 1013904223u;
        return (state >> 8) * (1.0f / 16777216.0f);
    }
    float range(float a, float b) { return a + next01() * (b - a); }
    bool chance(float p) { return next01() < p; }
};

float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// What part of a cave a spot belongs to: decides what grows there.
enum Zone { Gallery, Hall, Descent, Cavern, Grotto, Geode };

// A spot on a floor where something might grow.
struct Site { float s, floor; Zone zone; };

struct Builder {
    Rng& rng;
    float angle0;                  // mesa center, degrees
    float degPerUnit;              // degrees of arc per world unit of ground here
    float s0, ds;                  // ground table: first s, spacing
    const std::vector<float>& G;   // radius of the smoothed ground under the mesa

    std::vector<CarveMap::Op> ops;
    std::vector<float> opS;        // each bite's s, for the lookup grid
    std::vector<Site> sites;

    float angleAt(float s) const { return angle0 + s * degPerUnit; }
    float ground(float s) const {
        float f = clampf((s - s0) / ds, 0.0f, (float)G.size() - 1.001f);
        int i = (int)f;
        return G[i] + (G[i + 1] - G[i]) * (f - i);
    }
    Vec2 at(float s, float h) const {
        float a = angleAt(s), r = ground(s) + h;
        return Vec2(cosd(a) * r, sind(a) * r);
    }
    void bite(float s, float h, float r) {
        ops.push_back({at(s, h), r});
        opS.push_back(s);
    }

    // A passage whose floor runs from (sa, ha) to (sb, hb), easing in height at
    // both ends so it meets what it joins level. `rough` adds pockets in the
    // ceiling.
    void passage(float sa, float ha, float sb, float hb, float ra, float rb, float rough, Zone zone) {
        float len = sqrtf((sb - sa) * (sb - sa) + (hb - ha) * (hb - ha));
        int n = std::max(1, (int)(len / 4.0f));
        float ph = rng.range(0.0f, 6.28f), wave = rng.range(0.045f, 0.075f);
        float nextSite = rng.range(8.0f, 20.0f);
        for (int i = 0; i <= n; i++) {
            float u = (float)i / n, e = u * u * (3.0f - 2.0f * u);
            float s = sa + (sb - sa) * u, h = ha + (hb - ha) * e;
            float r = ra + (rb - ra) * u + 3.0f * sinf(len * u * wave + ph);
            if (r < 21.0f) r = 21.0f;
            bite(s, h + r, r);
            if (rough > 0.0f && rng.chance(0.07f * rough)) {
                float rn = rng.range(10.0f, 17.0f), lift = rng.range(4.0f, rn * 0.9f);
                bite(s + rng.range(-6.0f, 6.0f), h + 2.0f * r + lift - rn, rn);
            }
            if (len * u >= nextSite) {
                sites.push_back({s, h, zone});
                nextSite += rng.range(15.0f, 27.0f);
            }
        }
    }

    // A room `w` wide and up to `ht` tall standing on a level floor at hf:
    // a run of small bites for the floor itself, then domes of different
    // sizes over it — tallest in the middle — and pockets in the roof.
    void room(float sc, float hf, float w, float ht, float rough, Zone zone) {
        int n = std::max(1, (int)(w / 4.0f));
        for (int i = 0; i <= n; i++) bite(sc - w * 0.5f + w * i / n, hf + 23.0f, 23.0f);
        auto profile = [&](float x) { return 0.45f + 0.55f * (0.5f + 0.5f * cosf(PI * x / (w * 1.05f))); };
        float x = -w * 0.5f + 20.0f;
        while (x <= w * 0.5f - 20.0f) {
            float R = clampf(ht * 0.5f * profile(x) * rng.range(0.82f, 1.0f), 24.0f, w * 0.5f);
            bite(sc + x, hf + R, R);
            x += R * rng.range(0.35f, 0.55f);
        }
        int pockets = (int)(w / 42.0f * rough);
        for (int i = 0; i < pockets; i++) {
            float px = rng.range(-0.42f, 0.42f) * w;
            float rn = rng.range(12.0f, 22.0f), lift = rng.range(4.0f, rn);
            bite(sc + px, hf + ht * profile(px) * 0.8f + lift - rn, rn);
        }
        for (float sx = -w * 0.5f + rng.range(6.0f, 14.0f); sx < w * 0.5f; sx += rng.range(11.0f, 18.0f))
            sites.push_back({sc + sx, hf, zone});
    }

    // A wandering vertical shaft from ha up to hb, widening by `flare` over
    // its last 60 units (a funnel where it breaks the surface).
    void shaft(float s, float ha, float hb, float r, float flare) {
        float ph = rng.range(0.0f, 6.28f);
        for (float h = ha; h <= hb; h += 5.0f) {
            float t = clampf((h - (hb - 60.0f)) / 60.0f, 0.0f, 1.0f);
            bite(s + 7.0f * sinf(h * 0.035f + ph), h, r + flare * t * t);
        }
    }
};

// Distance to the cave's open space at a point (negative inside it): the same
// min-over-bites the carve layer starts from, with the bites binned along the
// ground so a query only looks at the ones nearby. Good enough to find floors,
// ceilings and walls for furnishing.
struct CaveSdf {
    const std::vector<CarveMap::Op>& ops;
    float sMin, cell;
    std::vector<std::vector<int>> bins;

    CaveSdf(const std::vector<CarveMap::Op>& o, const std::vector<float>& opS, float lo, float hi)
        : ops(o), sMin(lo), cell(48.0f) {
        bins.resize((size_t)((hi - lo) / cell) + 2);
        for (size_t k = 0; k < ops.size(); k++) {
            float reach = ops[k].radius * 1.2f + 10.0f;
            int b0 = std::max(0, (int)((opS[k] - reach - sMin) / cell));
            int b1 = std::min((int)bins.size() - 1, (int)((opS[k] + reach - sMin) / cell));
            for (int b = b0; b <= b1; b++) bins[b].push_back((int)k);
        }
    }
    float operator()(float s, Vec2 p) const {
        int b = (int)((s - sMin) / cell);
        if (b < 0 || b >= (int)bins.size()) return 1.0e4f;
        float best = 1.0e4f;
        for (int k : bins[b]) {
            float dx = p.x - ops[k].local.x, dy = p.y - ops[k].local.y;
            float v = sqrtf(dx * dx + dy * dy) - ops[k].radius;
            if (v < best) best = v;
        }
        return best;
    }
};

}  // namespace

void Planet::buildCaves() {
    cavesBuilt_ = true;
    caveProps.clear();
    caveMouths.clear();
    if (mesas.empty()) return;

    std::vector<CarveMap::Op> natural;
    using Kind = CaveProp::Kind;

    for (size_t mi = 0; mi < mesas.size(); mi++) {
        const Mesa& m = mesas[mi];
        Rng rng(seed ^ (0x63617665u + (unsigned)mi * 7919u));
        if (rng.chance(0.15f)) continue;                      // a few mesas are solid

        // The mesa in true world units: its angles are laid out at baseRadius,
        // the ground it stands on is usually a good deal further out.
        float groundR = surfaceRadius(m.angle) - m.height;
        float k = groundR / baseRadius;
        float W = m.half * k, E = m.edge * k, H = m.height;
        float degPerUnit = 180.0f / (PI * groundR);

        // The ground the mesa stands on: the field's own outline minus what
        // the mesa adds, averaged over a few hundred units (the grid blurs the
        // flanks, so the difference is lumpy right at them).
        const float L = W + 780.0f, DS = 8.0f;
        std::vector<float> G((size_t)(2.0f * L / DS) + 2);
        for (size_t i = 0; i < G.size(); i++) {
            float a = m.angle + (-L + i * DS) * degPerUnit, sum = 0.0f;
            for (int j = -4; j <= 4; j++) {
                float aj = a + j * 40.0f * degPerUnit;
                sum += surfaceRadius(aj) - mesaOffset(aj);
            }
            G[i] = sum / 9.0f;
        }
        Builder b{rng, m.angle, degPerUnit, -L, DS, G, {}, {}, {}};
        auto topAt = [&](float s) { return surfaceRadius(b.angleAt(s)) - b.ground(s); };

        // ---- Layout -----------------------------------------------------------
        // Where a flank has risen far enough to cut into: just in from the
        // foot, so the mouth opens in the slope and not as a trench before it.
        auto riseAt = [&](float x) {
            float t = clampf((W - x) / E, 0.0f, 1.0f);
            return H * t * t * (3.0f - 2.0f * t);
        };
        float xIn = W;
        while (xIn > 0.0f && riseAt(xIn) < 6.0f) xIn -= 2.0f;
        xIn += 10.0f;

        float e = rng.chance(0.5f) ? -1.0f : 1.0f;            // the flank the main mouth is in
        bool through = rng.chance(0.45f);
        float flat = W - E;                                   // half-width of the full-height top
        float climb = rng.next01() * std::min(30.0f, std::max(0.0f, H - 130.0f));
        float hallH = std::min(H - 44.0f - climb, rng.range(90.0f, 150.0f));
        float hallW = std::min(rng.range(220.0f, 360.0f), 2.0f * flat - 100.0f);
        float sH = rng.range(-1.0f, 1.0f) * std::max(0.0f, flat - hallW * 0.5f - 20.0f) * 0.7f;

        // Mouth (flared), gallery, hall.
        float sE = e * xIn, sM = sE - e * 110.0f;
        float nearEdge = sH + e * (hallW * 0.5f - 40.0f), farEdge = sH - e * (hallW * 0.5f - 40.0f);
        b.passage(sE, -1.0f, sM, 0.0f, 36.0f, 26.0f, 1.0f, Gallery);
        b.passage(sM, 0.0f, nearEdge, climb, 26.0f, 27.0f, 1.0f, Gallery);
        b.room(sH, climb, hallW, hallH, 1.0f, Hall);
        if (through) {
            float sE2 = -e * xIn, sM2 = sE2 + e * 110.0f;
            b.passage(farEdge, climb, sM2, 0.0f, 27.0f, 26.0f, 1.0f, Gallery);
            b.passage(sM2, 0.0f, sE2, -1.0f, 26.0f, 36.0f, 1.0f, Gallery);
        }
        // Skylight: a shaft from the hall's roof out through the top, ending
        // well above the ground so its mouth is open air.
        if (rng.chance(through ? 0.4f : 0.7f)) {
            float sS = sH + rng.range(-0.25f, 0.25f) * hallW;
            b.shaft(sS, climb + hallH * 0.55f, topAt(sS) + 24.0f, 21.0f, 13.0f);
        }
        // A side pocket up off the gallery, where the mesa is tall enough to
        // hold one above it: a steep little ramp to a crystal nook.
        {
            float room = fabsf(flat * e - nearEdge);          // gallery under the full-height top
            if (H - climb >= 178.0f && room >= 230.0f && rng.chance(0.6f)) {
                float sp = nearEdge + e * rng.range(110.0f, room - 110.0f);
                b.passage(sp, climb * 0.5f, sp + e * 85.0f, 64.0f, 22.0f, 22.0f, 0.0f, Gallery);
                b.room(sp + e * 110.0f, 64.0f, 110.0f, std::min(62.0f, H - 64.0f - 46.0f), 0.8f, Grotto);
            }
        }

        // ---- The deep -----------------------------------------------------------
        if (rng.chance(0.85f)) {
            const float limit = W + 600.0f;                   // as far out as the ground table goes, with room
            float D1 = rng.range(140.0f, 260.0f);             // cavern roof-to-ground and then some
            float ledge = rng.chance(0.5f) ? rng.range(25.0f, 50.0f) : 0.0f;
            float cavW = rng.range(320.0f, 560.0f);
            float cavH = std::min(rng.range(120.0f, 200.0f), D1 + ledge - 52.0f);
            float hc = -D1 - ledge;                           // cavern floor
            float sC;                                         // cavern center
            float d;                                          // the way the system heads as it goes down
            if (through) {
                // Both ends of the hall are taken: a well in its floor drops
                // into a cavern directly underneath.
                d = rng.chance(0.5f) ? -1.0f : 1.0f;
                float sW = sH + rng.range(-0.2f, 0.2f) * hallW;
                sC = sW + rng.range(-0.15f, 0.15f) * cavW;
                b.room(sC, hc, cavW, cavH, 1.4f, Cavern);
                b.shaft(sW, hc + cavH * 0.45f, climb + 8.0f, 21.0f, 0.0f);
            } else {
                // A ramp down out of the hall's free end, arriving in the
                // cavern's side — sometimes on a ledge above its floor.
                d = -e;
                float sA = farEdge - d * 10.0f;
                float run = D1 * rng.range(1.9f, 2.4f);
                float sB = sA + d * run;
                cavW = std::min(cavW, limit - fabsf(sB) + 90.0f);
                sC = sB + d * (cavW * 0.5f - 90.0f);
                if (cavW >= 240.0f) {
                    b.passage(sA, climb, sB, -D1, 25.0f, 26.0f, 1.0f, Descent);
                    b.room(sC, hc, cavW, cavH, 1.4f, Cavern);
                } else {
                    cavW = 0.0f;
                }
            }
            if (cavW > 0.0f) {
                // Off the cavern's ends: a grotto through a low crawl, and/or a
                // second ramp down to the geode room.
                auto roomFor = [&](float dir, float need) {
                    return fabsf(sC + dir * (cavW * 0.5f + need)) <= limit;
                };
                auto grotto = [&](float dir) {
                    float g0 = sC + dir * (cavW * 0.5f - 30.0f);
                    float g1 = g0 + dir * rng.range(70.0f, 130.0f);
                    float drop = rng.range(5.0f, 30.0f);
                    float gw = rng.range(120.0f, 170.0f), gh = rng.range(70.0f, 100.0f);
                    b.passage(g0, hc, g1, hc - drop, 22.0f, 21.0f, 0.5f, Descent);
                    b.room(g1 + dir * (gw * 0.5f - 25.0f), hc - drop, gw, gh, 1.2f, Grotto);
                };
                bool deeper = rng.chance(0.45f);
                float gd = d;                                 // the end the second level leaves from
                if (deeper && !roomFor(gd, 620.0f)) gd = -gd;
                if (deeper && roomFor(gd, 620.0f) && !(gd == -d && !through)) {
                    float D2 = rng.range(110.0f, 170.0f);
                    float g0 = sC + gd * (cavW * 0.5f - 30.0f);
                    float g1 = g0 + gd * D2 * rng.range(1.9f, 2.3f);
                    float gw = rng.range(220.0f, 300.0f), gh = std::min(rng.range(90.0f, 125.0f), D2 - 42.0f);
                    b.passage(g0, hc, g1, hc - D2, 25.0f, 25.0f, 1.0f, Descent);
                    b.room(g1 + gd * (gw * 0.5f - 60.0f), hc - D2, gw, gh, 1.3f, Geode);
                    if (roomFor(-gd, 330.0f) && !(-gd == -d && !through) && rng.chance(0.6f)) grotto(-gd);
                } else {
                    if (roomFor(d, 330.0f)) grotto(d);
                    if (through && roomFor(-d, 330.0f) && rng.chance(0.5f)) grotto(-d);
                }
            }
        }

        // ---- Furnish --------------------------------------------------------------
        CaveSdf sdf(b.ops, b.opS, -L, L);
        auto addProp = [&](Vec2 local, float dir, float size, Kind kind, bool inside, float frontChance) {
            CaveProp q{};
            q.local = local;
            q.dir = dir;
            q.size = size;
            q.phase = rng.range(0.0f, 6.2831853f);
            q.kind = kind;
            q.front = rng.chance(frontChance);
            q.inside = inside;
            q.hp = 1.0f;
            q.goneFor = 0.0f;
            caveProps.push_back(q);
        };
        // Search along a line of constant s for where the open space ends.
        auto edgeH = [&](float s, float hOpen, float step, float reach) {
            float lo = hOpen;
            for (float t = step; fabsf(t) <= reach; t += step) {
                float hi = hOpen + t;
                if (sdf(s, b.at(s, hi)) >= 0.0f) {
                    for (int it = 0; it < 8; it++) {
                        float mid = 0.5f * (lo + hi);
                        if (sdf(s, b.at(s, mid)) >= 0.0f) hi = mid; else lo = mid;
                    }
                    return 0.5f * (lo + hi);
                }
                lo = hi;
            }
            return 1.0e9f;      // nothing within reach
        };
        // What grows where: chances per site, by zone.
        struct Mix { float tite, mite, column, glowcap, crystal, gold; float crystalSize; };
        static const Mix mixes[] = {
            /* Gallery */ {0.30f, 0.20f, 0.04f, 0.05f, 0.04f, 0.00f, 18.0f},
            /* Hall    */ {0.40f, 0.24f, 0.06f, 0.08f, 0.06f, 0.00f, 22.0f},
            /* Descent */ {0.30f, 0.18f, 0.03f, 0.10f, 0.07f, 0.03f, 20.0f},
            /* Cavern  */ {0.42f, 0.26f, 0.07f, 0.12f, 0.09f, 0.04f, 26.0f},
            /* Grotto  */ {0.20f, 0.10f, 0.00f, 0.14f, 0.34f, 0.06f, 28.0f},
            /* Geode   */ {0.26f, 0.14f, 0.03f, 0.08f, 0.42f, 0.14f, 32.0f},
        };
        for (const Site& site : b.sites) {
            float s = site.s, a = b.angleAt(s);
            const Mix& mx = mixes[site.zone];
            if (sdf(s, b.at(s, site.floor + 14.0f)) > -5.0f) continue;        // not open here after all
            // Inside enough to be hidden from the surface (the mouth's own
            // rim is furnished separately, below).
            if (surfaceRadius(a) - (b.ground(s) + site.floor) < 36.0f) continue;
            float hCeil = edgeH(s, site.floor + 14.0f, 3.0f, 300.0f);
            float hFloor = edgeH(s, site.floor + 14.0f, -3.0f, 60.0f);
            if (hCeil > 1.0e8f || hFloor > 1.0e8f) continue;
            float gap = hCeil - hFloor;
            // A "ceiling" that is open air (a skylight, a mouth) grows nothing.
            Vec2 above = b.at(s, hCeil + 4.0f);
            bool roof = terrainAt(Vec2(center.x + above.x, center.y + above.y)) > 0.0f;
            Vec2 floorPt = b.at(s, hFloor), ceilPt = b.at(s, hCeil);

            if (roof && gap >= 46.0f && gap <= 105.0f && rng.chance(mx.column)) {
                addProp(floorPt, a, gap, Kind::Column, true, 0.3f);
                continue;
            }
            if (roof && rng.chance(mx.tite))
                addProp(ceilPt, a + 180.0f, std::min(gap * 0.42f, rng.range(14.0f, 30.0f + gap * 0.18f)),
                        Kind::Stalactite, true, 0.2f);
            if (rng.chance(mx.mite))
                addProp(floorPt, a, std::min(gap * 0.33f, rng.range(10.0f, 24.0f + gap * 0.10f)),
                        Kind::Stalagmite, true, 0.2f);
            if (rng.chance(mx.glowcap))
                addProp(floorPt, a, rng.range(9.0f, 15.0f), Kind::Glowcap, true, 0.3f);
            bool crystal = rng.chance(mx.crystal), gold = rng.chance(mx.gold);
            for (int pass = 0; pass < 2; pass++) {
                if (pass == 0 ? !crystal : !gold) continue;
                Kind kind = pass == 0 ? Kind::Crystal : Kind::GoldVein;
                float size = pass == 0 ? mx.crystalSize * rng.range(0.7f, 1.2f) : rng.range(11.0f, 17.0f);
                float where = rng.next01();
                if (pass == 0 && where < 0.45f) {
                    addProp(floorPt, a + rng.range(-14.0f, 14.0f), size, kind, true, 0.2f);
                } else if (where < 0.75f && roof) {
                    addProp(ceilPt, a + 180.0f + rng.range(-14.0f, 14.0f), size, kind, true, 0.15f);
                } else {
                    // On a wall: out sideways from partway up, to where the rock starts.
                    float side = rng.chance(0.5f) ? -1.0f : 1.0f;
                    float h = hFloor + rng.range(12.0f, std::max(14.0f, gap * 0.6f));
                    float lo = 0.0f, hit = -1.0f;
                    for (float t = 4.0f; t <= 260.0f; t += 4.0f) {
                        if (sdf(s + side * t, b.at(s + side * t, h)) >= 0.0f) { hit = t; break; }
                        lo = t;
                    }
                    if (hit < 0.0f) continue;
                    for (int it = 0; it < 6; it++) {
                        float mid = 0.5f * (lo + hit);
                        if (sdf(s + side * mid, b.at(s + side * mid, h)) >= 0.0f) hit = mid; else lo = mid;
                    }
                    float sw = s + side * hit;
                    // It grows back out of the wall, tipped a little upward.
                    addProp(b.at(sw, h), b.angleAt(sw) - side * rng.range(55.0f, 95.0f), size, kind, true, 0.15f);
                }
            }
        }

        // ---- Mouths ---------------------------------------------------------------
        // Wherever the cave has cut the ground's outline away. Each gets its
        // rim of rubble, loses whatever the surface had growing over it, and
        // is handed to the shader for its uneven edge.
        float runStart = 0.0f;
        bool open = false;
        for (float s = -L + 20.0f; s <= L - 20.0f; s += 4.0f) {
            float a = b.angleAt(s), r = surfaceRadius(a) - 2.0f;
            bool cut = sdf(s, Vec2(cosd(a) * r, sind(a) * r)) < 0.0f;
            if (cut && !open) { open = true; runStart = s; }
            if (!cut && open) {
                open = false;
                float sa = runStart - 4.0f, sb = s;
                caveMouths.push_back({b.angleAt(0.5f * (sa + sb)), 0.5f * (sb - sa) * degPerUnit + 14.0f * degPerUnit});
                for (int side = -1; side <= 1; side += 2) {
                    int n = 1 + (int)(rng.next01() * 2.6f);
                    float off = rng.range(5.0f, 14.0f);
                    for (int j = 0; j < n; j++) {
                        float sr = (side < 0 ? sa : sb) + side * off;
                        float ar = b.angleAt(sr), rr = surfaceRadius(ar);
                        addProp(Vec2(cosd(ar) * rr, sind(ar) * rr), ar, rng.range(10.0f, 24.0f), Kind::Boulder, false, 0.4f);
                        off += rng.range(14.0f, 30.0f);
                    }
                }
                // Plants and stones can't stand over an opening. (Marked gone
                // for good rather than erased, so indices into the list —
                // the laser's target, say — stay what they were.)
                float lo = b.angleAt(sa - 8.0f), hi = b.angleAt(sb + 8.0f);
                for (Harvestable& h : harvest) {
                    float da = std::fmod(h.angle - m.angle + 540.0f, 360.0f) - 180.0f;
                    if (m.angle + da < lo || m.angle + da > hi) continue;
                    h.grow = 0.0f;
                    h.regrowIn = 1.0e30f;
                }
            }
        }

        natural.insert(natural.end(), b.ops.begin(), b.ops.end());
    }
    if (natural.empty()) return;

    digOps_.insert(digOps_.begin(), natural.begin(), natural.end());
    naturalOps_ = (int)natural.size();
    carve.release();          // rebuilt (caves included) on the next focusCarve
}
