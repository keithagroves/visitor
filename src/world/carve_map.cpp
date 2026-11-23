#include "carve_map.h"
#include <algorithm>
#include <cmath>

namespace {
// Cut one bite into the raw (min over bites) layer. Returns the samples it
// changed.
CarveMap::Rect cutRaw(std::vector<float>& raw, float ox, float oy, const CarveMap::Op& op) {
    const int N = CarveMap::N;
    const float CELL = CarveMap::CELL;
    CarveMap::Rect rc;
    // Every sample whose distance to the bite could be smaller than what it
    // already holds: the bite itself plus a band around it, so the field stays
    // a usable distance (not just a mask) for anti-aliasing and collision.
    float reach = op.radius + CELL * 4.0f;
    int i0 = std::max(0, (int)floorf((op.local.x - reach - ox) / CELL));
    int i1 = std::min(N - 1, (int)ceilf((op.local.x + reach - ox) / CELL));
    int j0 = std::max(0, (int)floorf((op.local.y - reach - oy) / CELL));
    int j1 = std::min(N - 1, (int)ceilf((op.local.y + reach - oy) / CELL));
    if (i1 < i0 || j1 < j0) return rc;
    for (int j = j0; j <= j1; j++) {
        float dy = oy + j * CELL - op.local.y;
        for (int i = i0; i <= i1; i++) {
            float dx = ox + i * CELL - op.local.x;
            float v = sqrtf(dx * dx + dy * dy) - op.radius;   // < 0 inside the bite
            float& c = raw[(size_t)j * N + i];
            if (v < c) c = v;
        }
    }
    rc.i0 = i0; rc.j0 = j0; rc.i1 = i1; rc.j1 = j1;
    return rc;
}
}  // namespace

void CarveMap::recenter(Vec2 local, const std::vector<Op>& ops, const Terrain& terrain) {
    // Snap the origin to the sample lattice, so a bite lands on the same
    // samples (and gives the same hole) wherever the window happens to sit.
    float half = 0.5f * (N - 1) * CELL;
    ox_ = floorf((local.x - half) / CELL) * CELL;
    oy_ = floorf((local.y - half) / CELL) * CELL;
    raw_.assign((size_t)N * N, OPEN);
    terrain_.assign((size_t)N * N, NAN);
    // Cut everything first, then work out the inside distances once over the
    // area all of it touched.
    Rect all;
    all.i0 = N; all.j0 = N;
    for (const Op& op : ops) {
        Rect rc = cutRaw(raw_, ox_, oy_, op);
        if (rc.empty()) continue;
        all.i0 = std::min(all.i0, rc.i0); all.j0 = std::min(all.j0, rc.j0);
        all.i1 = std::max(all.i1, rc.i1); all.j1 = std::max(all.j1, rc.j1);
    }
    d_ = raw_;
    if (!all.empty()) refreshInterior(all.i0, all.j0, all.i1, all.j1, terrain);
    flooded_.assign((size_t)N * N, 0);
    wet_.assign((size_t)N * N, 0);
    wetDirty_ = Rect{};
    if (!all.empty()) flood(all.i0, all.j0, all.i1, all.j1, terrain, &ops);
}

bool CarveMap::contains(Vec2 local, float margin) const {
    if (d_.empty()) return false;
    float x = local.x - ox_, y = local.y - oy_;
    float span = (N - 1) * CELL;
    return x >= margin && y >= margin && x <= span - margin && y <= span - margin;
}

CarveMap::Rect CarveMap::apply(const Op& op, const Terrain& terrain) {
    if (d_.empty()) return Rect{};
    Rect rc = cutRaw(raw_, ox_, oy_, op);
    if (rc.empty()) return rc;
    // A sample's distance to rock reads the samples up to DEEP_CELLS away, so
    // the block whose inside distances may have changed is that much bigger.
    rc.i0 = std::max(0, rc.i0 - DEEP_CELLS);
    rc.j0 = std::max(0, rc.j0 - DEEP_CELLS);
    rc.i1 = std::min(N - 1, rc.i1 + DEEP_CELLS);
    rc.j1 = std::min(N - 1, rc.j1 + DEEP_CELLS);
    refreshInterior(rc.i0, rc.j0, rc.i1, rc.j1, terrain);
    flood(rc.i0, rc.j0, rc.i1, rc.j1, terrain, nullptr);
    return rc;
}

float CarveMap::terrainAt(int i, int j, const Terrain& terrain) {
    float& t = terrain_[(size_t)j * N + i];
    if (std::isnan(t)) t = terrain(Vec2(ox_ + i * CELL, oy_ + j * CELL));
    return t;
}

// For a sample p in open space near a hole: every sample q of real rock (not
// dug, not sky) has no open space within min(raw(q), terrain(q)) of it — raw
// is exact outside the holes, and the terrain density is close to a distance
// near the surface — so the disk of that radius around q is solid and the
// rock is at most |p - q| - that radius away. The smallest of those over the
// samples nearby is the distance to the rock, to within a fraction of a unit
// near a wall, which is where it matters.
//
// Only open samples within DEEP_CAP of a bite are worked out: further from
// every hole, sky is sky and min(terrain, raw) already says how far the
// ground is.
void CarveMap::refreshInterior(int i0, int j0, int i1, int j1, const Terrain& terrain) {
    const int K = DEEP_CELLS;
    for (int j = j0; j <= j1; j++) {
        for (int i = i0; i <= i1; i++) {
            size_t at = (size_t)j * N + i;
            float r = raw_[at];
            d_[at] = r;
            if (r >= DEEP_CAP) continue;                    // nowhere near a hole
            float t = terrainAt(i, j, terrain);
            if (r >= 0.0f && t > 0.0f) continue;            // rock
            float best = DEEP_CAP;
            int qi0 = std::max(0, i - K), qi1 = std::min(N - 1, i + K);
            int qj0 = std::max(0, j - K), qj1 = std::min(N - 1, j + K);
            for (int qj = qj0; qj <= qj1; qj++) {
                float dy = (float)(qj - j) * CELL;
                const float* row = &raw_[(size_t)qj * N];
                for (int qi = qi0; qi <= qi1; qi++) {
                    float v = row[qi];
                    if (v < 0.0f) continue;                 // dug: not rock
                    float dx = (float)(qi - i) * CELL;
                    float e = sqrtf(dx * dx + dy * dy);
                    if (e - std::min(v, DEEP_CAP) >= best) continue;   // can't win
                    float tq = terrainAt(qi, qj, terrain);
                    if (tq <= 0.0f) continue;               // sky: not rock
                    e -= std::min(std::min(v, tq), DEEP_CAP);
                    if (e < best) best = e;
                }
            }
            // Never nearer than the plain lower bounds: the edge of the dug
            // space (for a dug sample) and the terrain surface (for sky).
            float lower = std::max(-r, -t);
            d_[at] = -std::max(best, lower);
        }
    }
}

float CarveMap::sample(Vec2 local) const {
    if (d_.empty()) return OPEN;
    float gx = (local.x - ox_) / CELL, gy = (local.y - oy_) / CELL;
    if (gx < 0 || gy < 0 || gx >= N - 1 || gy >= N - 1) return OPEN;
    int i = (int)gx, j = (int)gy;
    float fx = gx - i, fy = gy - j;
    const float* row0 = &d_[(size_t)j * N + i];
    const float* row1 = row0 + N;
    float top = row0[0] * (1 - fx) + row0[1] * fx;
    float bot = row1[0] * (1 - fx) + row1[1] * fx;
    return top * (1 - fy) + bot * fy;
}

// ---- Water -------------------------------------------------------------------

void CarveMap::flood(int i0, int j0, int i1, int j1, const Terrain& terrain, const std::vector<Op>* ops) {
    if (seaR_ <= 0.0f || flooded_.empty()) return;
    const float seaSq = seaR_ * seaR_;
    auto below = [&](int i, int j) {
        float x = ox_ + i * CELL, y = oy_ + j * CELL;
        return x * x + y * y < seaSq;
    };
    // Dug out of what was rock (the sky over a hole's mouth is not "dug").
    auto dug = [&](int i, int j) { return raw_[(size_t)j * N + i] < 0.0f && terrainAt(i, j, terrain) > 0.0f; };
    // Water already: the sea (open above the ground, under the level), or a
    // flooded sample.
    auto water = [&](int i, int j) {
        if (i < 0 || j < 0 || i >= N || j >= N) return false;
        if (flooded_[(size_t)j * N + i]) return true;
        return below(i, j) && terrainAt(i, j, terrain) <= 0.0f;
    };
    std::vector<int> stack;
    auto take = [&](int i, int j) {
        flooded_[(size_t)j * N + i] = 1;
        stack.push_back(j * N + i);
    };
    for (int j = j0; j <= j1; j++) {
        for (int i = i0; i <= i1; i++) {
            if (flooded_[(size_t)j * N + i] || raw_[(size_t)j * N + i] >= 0.0f) continue;
            if (!below(i, j) || !dug(i, j)) continue;
            if (water(i - 1, j) || water(i + 1, j) || water(i, j - 1) || water(i, j + 1)) take(i, j);
        }
    }
    if (ops) {
        for (const Op& op : *ops) {
            if (!op.wet) continue;
            int i = (int)floorf((op.local.x - ox_) / CELL + 0.5f), j = (int)floorf((op.local.y - oy_) / CELL + 0.5f);
            if (i < 0 || j < 0 || i >= N || j >= N || flooded_[(size_t)j * N + i]) continue;
            if (below(i, j) && dug(i, j)) take(i, j);
        }
    }
    Rect& dr = wetDirty_;
    while (!stack.empty()) {
        int at = stack.back();
        stack.pop_back();
        int i = at % N, j = at / N;
        // Its own sample and the ring round it, in the mirror.
        int a0 = std::max(0, i - 1), a1 = std::min(N - 1, i + 1);
        int b0 = std::max(0, j - 1), b1 = std::min(N - 1, j + 1);
        for (int b = b0; b <= b1; b++)
            for (int a = a0; a <= a1; a++) wet_[(size_t)b * N + a] = 255;
        if (dr.empty()) { dr.i0 = a0; dr.j0 = b0; dr.i1 = a1; dr.j1 = b1; }
        else {
            dr.i0 = std::min(dr.i0, a0); dr.j0 = std::min(dr.j0, b0);
            dr.i1 = std::max(dr.i1, a1); dr.j1 = std::max(dr.j1, b1);
        }
        const int di[4] = {-1, 1, 0, 0}, dj[4] = {0, 0, -1, 1};
        for (int k = 0; k < 4; k++) {
            int qi = i + di[k], qj = j + dj[k];
            if (qi < 0 || qj < 0 || qi >= N || qj >= N) continue;
            if (flooded_[(size_t)qj * N + qi] || raw_[(size_t)qj * N + qi] >= 0.0f) continue;
            if (below(qi, qj) && dug(qi, qj)) take(qi, qj);
        }
    }
}

bool CarveMap::wetAt(Vec2 local) const {
    if (wet_.empty()) return false;
    int i = (int)floorf((local.x - ox_) / CELL + 0.5f), j = (int)floorf((local.y - oy_) / CELL + 0.5f);
    if (i < 0 || j < 0 || i >= N || j >= N) return false;
    return wet_[(size_t)j * N + i] != 0;
}

void CarveMap::markWetOps(std::vector<Op>& ops) const {
    if (flooded_.empty()) return;
    for (Op& op : ops) {
        if (op.wet) continue;
        int i = (int)floorf((op.local.x - ox_) / CELL + 0.5f), j = (int)floorf((op.local.y - oy_) / CELL + 0.5f);
        if (i < 0 || j < 0 || i >= N || j >= N) continue;
        if (flooded_[(size_t)j * N + i]) op.wet = true;
    }
}
