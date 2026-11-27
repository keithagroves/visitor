#include "plume.h"

#include <algorithm>
#include <cmath>

Plume::Plume() {
    const int n = S * S;
    u_.assign(n, 0.0f);    v_.assign(n, 0.0f);
    u0_.assign(n, 0.0f);   v0_.assign(n, 0.0f);
    fire_.assign(n, 0.0f); smoke_.assign(n, 0.0f); s0_.assign(n, 0.0f);
    pA_.assign(n, 0.0f);   pB_.assign(n, 0.0f);  div_.assign(n, 0.0f);
    solid_.assign(n, 0);   mask0_.assign(n, 0);
    tex_.assign(n * 2, 0.0f);
}

void Plume::clear() {
    std::fill(u_.begin(), u_.end(), 0.0f);
    std::fill(v_.begin(), v_.end(), 0.0f);
    std::fill(fire_.begin(), fire_.end(), 0.0f);
    std::fill(smoke_.begin(), smoke_.end(), 0.0f);
    std::fill(pA_.begin(), pA_.end(), 0.0f);
    std::fill(pB_.begin(), pB_.end(), 0.0f);
    live_ = false;
}

// The grid is somewhere else now: every cell's ground has to be looked at
// again (or there is none: open sky stays open).
void Plume::forgetGround() {
    std::fill(solid_.begin(), solid_.end(), (uint8_t)(ground_ ? 2 : 0));
    adrift_ = 0.0f;
}

// Move every field by whole cells: what was at (i + di, j + dj) is now at
// (i, j); what comes in from outside is still air.
void Plume::shift(int di, int dj) {
    if (std::abs(di) >= S || std::abs(dj) >= S) { clear(); forgetGround(); return; }
    auto move = [&](std::vector<float>& f) {
        for (int j = 0; j < S; j++) {
            int sj = j + dj;
            for (int i = 0; i < S; i++) {
                int si = i + di;
                s0_[IX(i, j)] = (si >= 0 && si < S && sj >= 0 && sj < S) ? f[IX(si, sj)] : 0.0f;
            }
        }
        f.swap(s0_);
    };
    move(u_); move(v_); move(fire_); move(smoke_);
    // The ground goes with it; the cells that came in have not been looked at.
    const uint8_t fresh = ground_ ? 2 : 0;
    for (int j = 0; j < S; j++) {
        int sj = j + dj;
        for (int i = 0; i < S; i++) {
            int si = i + di;
            mask0_[IX(i, j)] = (si >= 0 && si < S && sj >= 0 && sj < S) ? solid_[IX(si, sj)] : fresh;
        }
    }
    solid_.swap(mask0_);
}

bool Plume::follow(Vec2 focus, Vec2 drift) {
    const float half = span() * 0.5f;
    if (!live_) {
        // Nothing in it: just put it where the ship is.
        origin_ = Vec2(focus.x - half, focus.y - half);
        forgetGround();
        return true;
    }
    bool moved = false;
    origin_ += drift;
    // Drifting, the cells slide over the ground: once that adds up to half a
    // cell, what was marked is stale.
    adrift_ += std::fabs(drift.x) + std::fabs(drift.y);
    if (adrift_ > CELL * 0.5f) { forgetGround(); moved = true; }
    // Off the middle by a whole cell or more: shift by whole cells, so the
    // gas keeps its place in the world to the last decimal.
    int di = (int)((focus.x - (origin_.x + half)) / CELL);
    int dj = (int)((focus.y - (origin_.y + half)) / CELL);
    if (di == 0 && dj == 0) return moved;
    shift(di, dj);
    origin_.x += di * CELL;
    origin_.y += dj * CELL;
    return true;
}

void Plume::markGround(const std::function<bool(Vec2)>& solidAt, bool all) {
    if (!ground_) all = true;
    ground_ = true;
    anySolid_ = false;
    for (int j = 0; j < S; j++) {
        for (int i = 0; i < S; i++) {
            uint8_t& s = solid_[IX(i, j)];
            if (all || s == 2)
                s = solidAt(Vec2(origin_.x + (i + 0.5f) * CELL, origin_.y + (j + 0.5f) * CELL)) ? 1 : 0;
            anySolid_ = anySolid_ || s;
        }
    }
    if (all) adrift_ = 0.0f;
}

void Plume::openSky() {
    ground_ = false;
    anySolid_ = false;
    std::fill(solid_.begin(), solid_.end(), 0);
}

Vec2 Plume::openAbove(Vec2 p, Vec2 up, float reach) const {
    for (float lift = 0.0f; lift <= reach; lift += CELL * 0.5f) {
        Vec2 q = p + up * lift;
        int i = (int)std::floor((q.x - origin_.x) / CELL), j = (int)std::floor((q.y - origin_.y) / CELL);
        if (i < 1 || i > N || j < 1 || j > N) return p;
        if (!solid_[IX(i, j)]) return q;
    }
    return p;
}

float Plume::groundAlong(Vec2 p, Vec2 dir, float reach) const {
    for (float t = 0.0f; t <= reach; t += CELL * 0.5f) {
        Vec2 q = p + dir * t;
        int i = (int)std::floor((q.x - origin_.x) / CELL), j = (int)std::floor((q.y - origin_.y) / CELL);
        if (i < 1 || i > N || j < 1 || j > N) return -1.0f;
        if (solid_[IX(i, j)] == 1) return t;
    }
    return -1.0f;
}

void Plume::inject(Vec2 from, Vec2 to, Vec2 vel, float radius, float fire, float smoke, float pull) {
    const float rc = std::max(radius / CELL, 0.6f);
    const Vec2 d = to - from;
    const int steps = std::clamp((int)std::ceil(d.length() / (CELL * 0.75f)), 1, 24);
    const float uc = vel.x / CELL, vc = vel.y / CELL;
    const int reach = (int)std::ceil(rc * 2.2f);
    for (int k = 0; k < steps; k++) {
        Vec2 p = from + d * ((k + 1) / (float)steps);
        float gx = (p.x - origin_.x) / CELL - 0.5f, gy = (p.y - origin_.y) / CELL - 0.5f;
        int ci = (int)std::floor(gx + 0.5f), cj = (int)std::floor(gy + 0.5f);
        for (int j = cj - reach; j <= cj + reach; j++) {
            if (j < 1 || j > N) continue;
            for (int i = ci - reach; i <= ci + reach; i++) {
                if (i < 1 || i > N) continue;
                int id = IX(i, j);
                if (solid_[id]) continue;
                float dx = i - gx, dy = j - gy;
                float w = std::exp(-(dx * dx + dy * dy) / (rc * rc));
                if (w < 0.01f) continue;
                // A nozzle only holds so much: hovering in place must not
                // pile the fire up without end.
                fire_[id] = std::min(fire_[id] + fire * w / steps, 1.5f);
                smoke_[id] = std::min(smoke_[id] + smoke * w / steps, 1.5f);
                float k2 = std::min(1.0f, pull * w);
                u_[id] += (uc - u_[id]) * k2;
                v_[id] += (vc - v_[id]) * k2;
            }
        }
    }
    live_ = true;
}

// The grid's edge is open: the flow out of it is whatever the flow just
// inside is. A cell in the ground is a wall: nothing moves in it, and the
// cells beside it have no flow into it.
void Plume::walls() {
    for (int k = 1; k <= N; k++) {
        u_[IX(0, k)] = u_[IX(1, k)];         v_[IX(0, k)] = v_[IX(1, k)];
        u_[IX(N + 1, k)] = u_[IX(N, k)];     v_[IX(N + 1, k)] = v_[IX(N, k)];
        u_[IX(k, 0)] = u_[IX(k, 1)];         v_[IX(k, 0)] = v_[IX(k, 1)];
        u_[IX(k, N + 1)] = u_[IX(k, N)];     v_[IX(k, N + 1)] = v_[IX(k, N)];
    }
    if (!anySolid_) return;
    for (int j = 1; j <= N; j++) {
        for (int i = 1; i <= N; i++) {
            int id = IX(i, j);
            if (solid_[id]) { u_[id] = 0.0f; v_[id] = 0.0f; continue; }
            if (solid_[id - 1] && u_[id] < 0.0f) u_[id] = 0.0f;
            if (solid_[id + 1] && u_[id] > 0.0f) u_[id] = 0.0f;
            if (solid_[id - S] && v_[id] < 0.0f) v_[id] = 0.0f;
            if (solid_[id + S] && v_[id] > 0.0f) v_[id] = 0.0f;
        }
    }
}

// x - a * laplacian(x) = x0, by Gauss-Seidel sweeps (the walkthrough's
// diffuse step). Stable however big `a` is.
void Plume::diffuse(std::vector<float>& x, const std::vector<float>& x0, float a, int iters, bool velocity) {
    if (a <= 0.0f) return;
    const float inv = 1.0f / (1.0f + 4.0f * a);
    for (int it = 0; it < iters; it++) {
        for (int j = 1; j <= N; j++) {
            for (int i = 1; i <= N; i++) {
                int id = IX(i, j);
                x[id] = (x0[id] + a * (x[id - 1] + x[id + 1] + x[id - S] + x[id + S])) * inv;
            }
        }
        // (Between sweeps the walls have to hold; after the last one the
        // projection that follows sees to them.)
        if (velocity && it + 1 < iters) walls();
    }
}

// Semi-Lagrangian advection: what is in a cell now is what was where the
// flow came from, a step ago (bilinear between the four cells there).
static void advectField(std::vector<float>& d, const std::vector<float>& d0, const std::vector<float>& uu,
                        const std::vector<float>& vv, const std::vector<uint8_t>& solid, float dt) {
    const int N = Plume::N, S = Plume::S;
    for (int j = 1; j <= N; j++) {
        for (int i = 1; i <= N; i++) {
            int id = i + S * j;
            if (solid[id]) { d[id] = 0.0f; continue; }
            float x = std::clamp(i - dt * uu[id], 0.5f, N + 0.5f);
            float y = std::clamp(j - dt * vv[id], 0.5f, N + 0.5f);
            int i0 = (int)x, j0 = (int)y;
            float s1 = x - i0, s0 = 1.0f - s1, t1 = y - j0, t0 = 1.0f - t1;
            int a = i0 + S * j0;
            d[id] = s0 * (t0 * d0[a] + t1 * d0[a + S]) + s1 * (t0 * d0[a + 1] + t1 * d0[a + 1 + S]);
        }
    }
}

// Make the flow incompressible: find the pressure whose gradient takes the
// divergence out of it, and subtract that. Pressure is zero at the open edge;
// a wall takes the pressure of the cell beside it (so nothing is pushed
// through it).
//
// The solve is a few Gauss-Seidel sweeps starting from `p`, the answer this
// same solve found last frame: the jet sits at the same place in the grid from
// one frame to the next, so that is most of the way there already.
void Plume::project(std::vector<float>& p_) {
    for (int j = 1; j <= N; j++) {
        for (int i = 1; i <= N; i++) {
            int id = IX(i, j);
            div_[id] = solid_[id] ? 0.0f : -0.5f * (u_[id + 1] - u_[id - 1] + v_[id + S] - v_[id - S]);
        }
    }
    const int iters = tune.pressureIters;
    if (!anySolid_) {
        for (int it = 0; it < iters; it++)
            for (int j = 1; j <= N; j++)
                for (int i = 1; i <= N; i++) {
                    int id = IX(i, j);
                    p_[id] = (div_[id] + p_[id - 1] + p_[id + 1] + p_[id - S] + p_[id + S]) * 0.25f;
                }
        for (int j = 1; j <= N; j++)
            for (int i = 1; i <= N; i++) {
                int id = IX(i, j);
                u_[id] -= 0.5f * (p_[id + 1] - p_[id - 1]);
                v_[id] -= 0.5f * (p_[id + S] - p_[id - S]);
            }
    } else {
        for (int it = 0; it < iters; it++)
            for (int j = 1; j <= N; j++)
                for (int i = 1; i <= N; i++) {
                    int id = IX(i, j);
                    if (solid_[id]) continue;
                    float sum = div_[id];
                    int open = 0;
                    if (!solid_[id - 1]) { sum += p_[id - 1]; open++; }
                    if (!solid_[id + 1]) { sum += p_[id + 1]; open++; }
                    if (!solid_[id - S]) { sum += p_[id - S]; open++; }
                    if (!solid_[id + S]) { sum += p_[id + S]; open++; }
                    p_[id] = open ? sum / open : 0.0f;
                }
        for (int j = 1; j <= N; j++)
            for (int i = 1; i <= N; i++) {
                int id = IX(i, j);
                if (solid_[id]) continue;
                float c = p_[id];
                float l = solid_[id - 1] ? c : p_[id - 1], r = solid_[id + 1] ? c : p_[id + 1];
                float up = solid_[id - S] ? c : p_[id - S], dn = solid_[id + S] ? c : p_[id + S];
                u_[id] -= 0.5f * (r - l);
                v_[id] -= 0.5f * (dn - up);
            }
    }
    walls();
}

void Plume::step(float dt, Vec2 up, float air) {
    if (!live_) return;
    dt = std::clamp(dt, 0.2f, 2.0f);
    air = std::clamp(air, 0.0f, 1.0f);
    const Tuning& T = tune;

    // Forces. Hot gas rises (where there is air to be lighter than), and the
    // air it moves through slows it.
    {
        const float bf = T.buoyFire * air * dt, bs = T.buoySmoke * air * dt;
        const float keep = std::exp(-T.drag * air * dt);
        for (int j = 1; j <= N; j++)
            for (int i = 1; i <= N; i++) {
                int id = IX(i, j);
                float lift = bf * fire_[id] + bs * smoke_[id];
                u_[id] = (u_[id] + up.x * lift) * keep;
                v_[id] = (v_[id] + up.y * lift) * keep;
            }
    }
    // Vorticity confinement: push the flow round each little whirl, toward
    // where it spins fastest, so the curls a coarse grid would smear away
    // keep turning. (div_ holds the spin for the moment.)
    if (T.curl > 0.0f) {
        for (int j = 1; j <= N; j++)
            for (int i = 1; i <= N; i++) {
                int id = IX(i, j);
                div_[id] = 0.5f * ((v_[id + 1] - v_[id - 1]) - (u_[id + S] - u_[id - S]));
            }
        const float e = T.curl * dt;
        for (int j = 2; j < N; j++)
            for (int i = 2; i < N; i++) {
                int id = IX(i, j);
                float gx = 0.5f * (std::fabs(div_[id + 1]) - std::fabs(div_[id - 1]));
                float gy = 0.5f * (std::fabs(div_[id + S]) - std::fabs(div_[id - S]));
                float len = std::sqrt(gx * gx + gy * gy) + 1e-5f;
                float w = div_[id];
                u_[id] += e * (gy / len) * w;
                v_[id] -= e * (gx / len) * w;
            }
    }
    walls();

    // The velocity: diffuse -> project -> advect (carried by itself) -> project.
    u0_ = u_; v0_ = v_;
    diffuse(u_, u0_, T.viscosity * dt, T.diffuseIters, true);
    diffuse(v_, v0_, T.viscosity * dt, T.diffuseIters, true);
    project(pA_);
    u0_ = u_; v0_ = v_;
    advectField(u_, u0_, u0_, v0_, solid_, dt);
    advectField(v_, v0_, u0_, v0_, solid_, dt);
    project(pB_);

    // The stuff: diffuse -> advect (carried by the flow) -> fade.
    s0_ = fire_;  diffuse(fire_, s0_, T.spread * dt, T.diffuseIters, false);
    s0_ = fire_;  advectField(fire_, s0_, u_, v_, solid_, dt);
    s0_ = smoke_; diffuse(smoke_, s0_, T.spread * dt, T.diffuseIters, false);
    s0_ = smoke_; advectField(smoke_, s0_, u_, v_, solid_, dt);

    // Fire cools: into smoke where there is air, into nothing where there is
    // none (and what smoke there is thins out at once).
    const float fireKeep = std::exp(-dt / T.fireLife);
    const float smokeKeep = std::exp(-dt / (6.0f + (T.smokeLife - 6.0f) * air));
    const float toSmoke = T.smokeFromFire * air * dt;
    float most = 0.0f;
    for (int j = 1; j <= N; j++)
        for (int i = 1; i <= N; i++) {
            int id = IX(i, j);
            if (solid_[id]) { fire_[id] = 0.0f; smoke_[id] = 0.0f; continue; }
            float f = fire_[id];
            float s = smoke_[id] + f * toSmoke;
            f = std::max(0.0f, f * fireKeep - T.fireFade * dt);
            s = std::max(0.0f, s * smokeKeep - T.smokeFade * dt);
            fire_[id] = f;
            smoke_[id] = std::min(s, 1.5f);
            if (f > most) most = f;
            if (s > most) most = s;
        }
    // All gone: stop, until the engine lights again.
    if (most < 0.004f) clear();
}

const std::vector<float>& Plume::texels() {
    const int n = S * S;
    for (int id = 0; id < n; id++) {
        tex_[id * 2] = fire_[id];
        tex_[id * 2 + 1] = smoke_[id];
    }
    return tex_;
}
