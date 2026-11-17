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
