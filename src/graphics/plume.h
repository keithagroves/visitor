#pragma once

#include <cstdint>
#include <functional>
#include <vector>
#include "../math/vecmath.h"

// Rocket exhaust as a fluid.
//
// A small grid of gas around the ship, stepped with Jos Stam's "Stable
// Fluids" the way the walkthrough the project's references name does it
// (https://myzopotamia.dev/navier-stokes-fluid-simulation-explained-with-godot):
// every cell holds a velocity and how much stuff is in it, and each frame
//
//     diffuse the velocity -> project -> advect the velocity -> project
//     diffuse the stuff    -> advect the stuff -> fade it
//
// where "project" is the pressure solve that makes the flow incompressible:
// that is what makes a jet push the air aside, curl up into billows, and
// spread along the ground when it hits it. The engine is a source: each frame
// it puts hot gas into the cells behind the nozzle and sets them moving.
//
// Beyond the walkthrough, because this is an exhaust and not a box of smoke:
//   - two kinds of stuff: `fire` (bright, gone in a fraction of a second) and
//     `smoke` (what fire cools into where there is air; it lingers and rises);
//   - the grid's edge is open (gas leaves, nothing bounces back), and cells
//     inside the ground are walls;
//   - buoyancy, a little drag, and vorticity confinement, which puts back the
//     small curls a coarse grid smears away;
//   - the grid follows the ship, shifting by whole cells, and can drift with
//     the medium (see follow()).
//
// Pure simulation: no GL here. main.cpp feeds it (where the nozzle is, what
// is rock), uploads pixels() as a texture and draws it (kPlumeFrag).
// Positions are relative to the center of the world the ship is at;
// velocities are world units a frame, as everywhere else.
class Plume {
public:
    static constexpr int N = 88;            // cells a side that are simulated
    static constexpr int S = N + 2;         // ...plus a one-cell border
    static constexpr float CELL = 4.8f;     // world units a cell (the ship is about 60 long)

    struct Tuning {
        float viscosity = 0.040f;       // velocity diffusion a frame (the walkthrough's 0.01 at its scale)
        float spread = 0.025f;          // diffusion of fire and smoke a frame (its 0.006)
        int   diffuseIters = 1;         // Gauss-Seidel sweeps for each diffusion (the rates are tiny: one does it)
        int   pressureIters = 6;        // ...and for each pressure solve, which starts from last frame's answer
        float fireLife = 3.4f;          // frames for fire to fall to 1/e
        float fireFade = 0.012f;        // ...plus this much off it a frame, so thin wisps end
        float smokeLife = 150.0f;       // frames for smoke to fall to 1/e, in thick air
        float smokeFade = 0.0011f;
        float smokeFromFire = 0.31f;    // smoke made a frame, per unit of fire, in thick air
        float buoyFire = 0.020f;        // lift a frame, in cells a frame, per unit of fire / of smoke
        float buoySmoke = 0.0065f;
        float drag = 0.016f;            // share of its speed the gas loses a frame, in thick air
        float curl = 0.22f;             // vorticity confinement
    };
    Tuning tune;

    Plume();
    void clear();
    // Is there anything in it (worth stepping, worth drawing)?
    bool live() const { return live_; }

    // The corner of the grid (of border cell 0,0), and how far across it is.
    Vec2 origin() const { return origin_; }
    static constexpr float span() { return S * CELL; }

    // Keep the grid around `focus`. `drift` is how far the medium the gas
    // hangs in has moved this frame (nothing in air, which stays with the
    // ground; the ship's own motion in a vacuum, where there is nothing to
    // leave the exhaust behind in). Returns true when the grid moved, i.e.
    // when there are cells whose ground has to be looked at (markGround()).
    bool follow(Vec2 focus, Vec2 drift);

    // Say which cells are in the ground: `solidAt` is asked about the center
    // of each cell that has come into the grid since the last call — or of
    // every cell with `all` (the ground may have been dug).
    void markGround(const std::function<bool(Vec2)>& solidAt, bool all);
    // ...or that there is no ground anywhere near.
    void openSky();

    // The engine: gas along the stretch from `from` to `to` (the nozzle's path
    // this frame, so a fast ship leaves a streak, not dots), moving at `vel`
    // (in the grid's own frame). `radius` in world units; `fire` and `smoke`
    // are how much goes in; `pull` (0..1) is how firmly the cells there are
    // set to that velocity.
    void inject(Vec2 from, Vec2 to, Vec2 vel, float radius, float fire, float smoke, float pull);
    // `p`, or if its cell is in the ground the first point above it (along
    // `up`, at most `reach` world units) that is not: a nozzle sitting on the
    // pad puts its gas into the air just over it, never into the rock.
    Vec2 openAbove(Vec2 p, Vec2 up, float reach) const;
    // How far from `p` along `dir` the ground is (a cell marked as ground),
    // looking at most `reach` world units; -1 if it is not that close.
    float groundAlong(Vec2 p, Vec2 dir, float reach) const;

    // One frame. `dt` in frames (1 = a 60 Hz frame); `up` points away from
    // the ground; `air` is 0 (vacuum) .. 1 (thick air).
    void step(float dt, Vec2 up, float air);

    // S x S cells, two floats each (fire, smoke), for a texture. Row 0 is the
    // row at origin().y.
    const std::vector<float>& texels();

private:
    static int IX(int i, int j) { return i + S * j; }
    void diffuse(std::vector<float>& x, const std::vector<float>& x0, float a, int iters, bool velocity);
    void project(std::vector<float>& p);
    void walls();
    void shift(int di, int dj);
    void forgetGround();

    Vec2 origin_;
    bool live_ = false;
    bool ground_ = false;       // the caller marks ground (markGround), as opposed to open sky
    bool anySolid_ = false;
    float adrift_ = 0.0f;       // how far the grid has drifted since its ground was last marked
    std::vector<float> u_, v_, u0_, v0_;        // velocity (cells a frame) and its snapshot
    std::vector<float> fire_, smoke_, s0_;      // the stuff, and scratch
    std::vector<float> pA_, pB_, div_;          // the two pressure solves (each keeps its answer), divergence
    std::vector<uint8_t> solid_, mask0_;        // 0 open, 1 ground, 2 not looked at yet
    std::vector<float> tex_;
};
