#pragma once

#include <functional>
#include <vector>
#include "../math/vecmath.h"

// Fine-grained digging. The planet's TerrainField is far too coarse to dig at
// the scale of a person (its cells are 250 units; the astronaut is 36 tall), and
// refining it planet-wide would cost tens of megabytes. So digging lives in its
// own layer: a small high-resolution window (N x N samples, CELL units apart)
// that follows the player around, holding nothing but what has been cut away.
//
// Each sample is positive in untouched rock (its distance to the nearest dug
// space), OPEN where nothing has been dug nearby, and in open space near a hole
// — dug out, or the sky over its mouth — minus the distance to the nearest
// rock that is really still there. The rock that is actually there is
// min(terrain density, carve)  — see Planet::rockAt — which the body shader
// and the astronaut's collision both evaluate, so what you see cut away is
// exactly what you can walk into.
//
// The window only covers a few thousand units. Everything ever dug is kept as a
// list of bites (Planet::digOps_); when the player nears the window's edge it is
// re-centered and rebuilt from that list, so holes persist wherever they are.
// Coordinates are planet-local (relative to the planet's center), so the window
// rides along with the planet on its orbit for free.
class CarveMap {
public:
    static constexpr int N = 512;            // samples per side
    static constexpr float CELL = 6.0f;      // world units between samples
    static constexpr float OPEN = 1.0e4f;    // "nothing dug near here"

    // One circular bite, planet-local. `wet`: water has got into it (see
    // Water, below) — remembered with the bite, so a flooded tunnel is still
    // flooded when the window is rebuilt somewhere its link to the sea is out
    // of sight.
    struct Op { Vec2 local; float radius; bool wet = false; };
    struct Rect { int i0 = 0, j0 = 0, i1 = -1, j1 = -1; bool empty() const { return i1 < i0 || j1 < j0; } };
    // The terrain's density (> 0 rock) at a planet-local point: where the sky
    // is, so open space over a hole's mouth counts as open.
    using Terrain = std::function<float(Vec2)>;

    bool ready() const { return !d_.empty(); }
    void release() {
        d_.clear(); d_.shrink_to_fit();
        raw_.clear(); raw_.shrink_to_fit();
        terrain_.clear(); terrain_.shrink_to_fit();
        flooded_.clear(); flooded_.shrink_to_fit();
        wet_.clear(); wet_.shrink_to_fit();
        wetDirty_ = Rect{};
    }

    // Center the window on a planet-local point and rebuild it from every bite
    // that reaches into it.
    void recenter(Vec2 local, const std::vector<Op>& ops, const Terrain& terrain);
    // Is this point (with margin) inside the window?
    bool contains(Vec2 local, float margin) const;
    Vec2 center() const { return Vec2(ox_ + 0.5f * (N - 1) * CELL, oy_ + 0.5f * (N - 1) * CELL); }

    // Cut one bite. Returns the samples it changed (empty if it lies outside).
    Rect apply(const Op& op, const Terrain& terrain);

    // Signed distance to dug space at a planet-local point (bilinear). OPEN
    // outside the window.
    float sample(Vec2 local) const;

    // --- Water ----------------------------------------------------------------
    // A world's water stands at one level (a radius from its center; set it
    // before recenter). Above the ground that is simply the sea. Dug space
    // takes water only where it CONNECTS to some: a sample that has been dug,
    // lies below the level and touches water — the sea, or a flooded sample
    // next to it — floods, and so on along the tunnel. Water only ever gets
    // into more space, so each bite just carries on from what it opened.
    void setSea(float radius) { seaR_ = radius; }
    // Has the water got into the dug space at this point? (Says nothing of the
    // level: the caller compares the point's radius with it.)
    bool wetAt(Vec2 local) const;
    // After recenter/apply: mark the bites whose space is now flooded.
    void markWetOps(std::vector<Op>& ops) const;
    // GPU mirror: 255 on every flooded sample and the ring of samples round
    // it (so a bilinear read is solid right up to the tunnel's walls), N x N.
    const std::vector<unsigned char>& wet() const { return wet_; }
    // The samples of wet() changed since this was last asked.
    Rect takeWetDirty() { Rect r = wetDirty_; wetDirty_ = Rect{}; return r; }

    // --- GPU mirror: the samples as an N x N single-channel texture ---------
    float originX() const { return ox_; }   // planet-local position of sample (0,0)
    float originY() const { return oy_; }
    const std::vector<float>& data() const { return d_; }

private:
    // Inside a hole, how far out to measure the real distance to rock: past
    // the largest collision probe (the astronaut's chest, 8) plus the normal's
    // finite difference, and past the shader's mouth rounding (14). Deeper
    // than this reads as DEEP_CAP.
    static constexpr int   DEEP_CELLS = 5;
    static constexpr float DEEP_CAP   = 4.0f * CELL;

    // Rebuild the open-space distances of d_ over a block of samples.
    void refreshInterior(int i0, int j0, int i1, int j1, const Terrain& terrain);
    float terrainAt(int i, int j, const Terrain& terrain);   // cached per sample
    // Let the water into whatever dug space in this block of samples touches
    // some, and on from there. `ops`: bites already known to be flooded.
    void flood(int i0, int j0, int i1, int j1, const Terrain& terrain, const std::vector<Op>* ops);

    float ox_ = 0, oy_ = 0;
    // raw_: min over bites of (distance to the bite's center - its radius).
    // Exact distance to dug space OUTSIDE the holes, but inside them it is
    // only the depth within the single deepest bite — where two bites barely
    // overlap that is a few units in the middle of open space, which reads to
    // the collision as a floor or wall that isn't there.
    //
    // And raw_ knows nothing of the sky: it counts the open air just above a
    // hole's mouth as rock. A shaft cut up from below stops the moment it
    // breaks through, so its bites end a few units above the ground line, and
    // min(terrain, raw_) there put a lid across the mouth at head height that
    // nobody could see or climb past.
    //
    // d_: what is published (sample() and the texture): raw_ in rock, and in
    // open space near a hole (dug, or sky) minus the distance to the nearest
    // REAL rock — not dug, not sky.
    std::vector<float> raw_;                 // N*N, row-major
    std::vector<float> d_;                   // N*N, row-major
    std::vector<float> terrain_;             // N*N terrain density, NaN until first needed
    float seaR_ = 0.0f;                      // the water level, as a planet-local radius (0 = none)
    std::vector<unsigned char> flooded_;     // N*N: this dug sample is under water
    std::vector<unsigned char> wet_;         // N*N: flooded_, grown by one sample (the GPU mirror)
    Rect wetDirty_;
};
