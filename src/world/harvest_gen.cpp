// Planet::growHarvest: where the plants and minerals on the ground strip
// (Planet::harvest) grow.
//
// Scattering them evenly with a little jitter reads as exactly that: things
// dropped at random. What makes ground cover look grown is that it is patchy
// and that the patches are where they are for a reason:
//
//   - Density follows the ground. Plants like damp, level, low ground: a slow
//     "moisture" noise makes meadows and bare stretches, hollows are lusher,
//     steep slopes and high ground are nearly bare. Stone gathers where rock
//     is exposed or falls: on slopes, high up, at the foot of a mesa, and in
//     the mineral-rich stretches of its own slow noise.
//   - Things grow in clumps. Grass comes as a patch of tufts, tallest in the
//     middle and thinning at the edge, often with a bulb plant or two among
//     it; bulbs also stand in small groves. Rocks come as one boulder with
//     its scatter of stones, sometimes with crystals growing out of the pile;
//     crystals as a big one with smaller ones round its foot.
//   - A clump is a patch on the ground, not a row: its members share a place
//     in depth (in front of / behind the walking line) and spread a little
//     around it.
//   - And a few loners stand between the patches, so the gaps are not empty.
//
// How much grows in all is unchanged (about one thing per 85 units of ground
// on the home world, split by the world's flora / mineral densities).
#include "planet.h"
#include <algorithm>
#include <cmath>

namespace {

const float PI = 3.14159265f;

struct Rng {
    unsigned int state;
    explicit Rng(unsigned int s) : state(s ? s : 1) {}
    float next01() {
        state = state * 1664525u + 1013904223u;
        return (state >> 8) * (1.0f / 16777216.0f);
    }
    float range(float a, float b) { return a + next01() * (b - a); }
    bool chance(float p) { return next01() < p; }
    // Roughly normal, unit spread, within about +-3.5.
    float gauss() { return (next01() + next01() + next01() + next01() - 2.0f) * 1.732f; }
};

float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
float smooth(float e0, float e1, float x) {
    float t = clampf((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

using Kind = Planet::Harvestable::Kind;

}  // namespace

void Planet::growHarvest() {
    harvest.clear();
    float growth = look.floraDensity + look.mineralDensity;
    if (isSun || growth <= 0.0f) return;

    Rng rng(seed ^ 0x67726f77u);
    PerlinNoise perlin(seed ^ 0x51ed270bu);
    const float around = 2.0f * PI * baseRadius;

    // ---- The ground, as a table round the planet ------------------------------
    const int T = 8192;
    const float unit = around / T;                 // ground per entry
    std::vector<float> R(T);
    for (int i = 0; i < T; i++) R[i] = seedRadius(i * (360.0f / T));
    auto idx = [&](float a) {
        int i = (int)floorf(a / 360.0f * T);
        return ((i % T) + T) % T;
    };
    auto at = [&](int i) { return R[((i % T) + T) % T]; };
    // Steepness (rise over run) across ~80 units of ground.
    auto slopeAt = [&](float a) {
        int i = idx(a), d = std::max(1, (int)(40.0f / unit));
        return fabsf(at(i + d) - at(i - d)) / (2.0f * d * unit);
    };
    // How far the ground here sits below its surroundings (> 0 in a dip).
    auto hollowAt = [&](float a) {
        int i = idx(a), w = std::max(2, (int)(260.0f / unit));
        return 0.5f * (at(i + w) + at(i - w)) - at(i);
    };
    // 0 at the planet's base radius .. 1 at the top of its noise band — the
    // same measure the body shader picks the terrain tone by (snow up top).
    auto altAt = [&](float a) {
        return clampf((at(idx(a)) - baseRadius) / std::max(noiseIntensity, 1.0f), 0.0f, 1.0f);
    };
    // Slow noise round the planet, 0..1, with features about `length` long
    // (sampled on a circle in noise space, so it has no seam).
    auto slowNoise = [&](float a, float length, float ox, float oy) {
        float rad = a * (PI / 180.0f);
        float f1 = baseRadius / length, f2 = f1 * 2.7f;
        float n = 0.65f * perlin.noise(cosf(rad) * f1 + ox, sinf(rad) * f1 + oy) +
                  0.35f * perlin.noise(cosf(rad) * f2 + ox * 1.7f, sinf(rad) * f2 + oy * 1.7f);
        return 0.5f + 0.5f * n;
    };
    float mOx = rng.range(-50.0f, 50.0f), mOy = rng.range(-50.0f, 50.0f);
    float rOx = rng.range(-50.0f, 50.0f), rOy = rng.range(-50.0f, 50.0f);
    auto moisture = [&](float a) { return smooth(0.34f, 0.64f, slowNoise(a, 1500.0f, mOx, mOy)); };
    auto richness = [&](float a) { return smooth(0.42f, 0.62f, slowNoise(a, 1100.0f, rOx, rOy)); };
    // Just outside a mesa's foot, or on the lower part of its flank: where
    // what falls off it comes to rest.
    auto talus = [&](float a) {
        float arcPerDeg = baseRadius * (PI / 180.0f), best = 0.0f;
        for (const Mesa& m : mesas) {
            float x = fabsf(std::fmod(a - m.angle + 540.0f, 360.0f) - 180.0f) * arcPerDeg;
            float outside = x - m.half;                                  // > 0 beyond the foot
            float t = outside >= 0.0f ? 1.0f - smooth(20.0f, 190.0f, outside)
                                      : 1.0f - smooth(0.0f, m.edge * 0.55f, -outside);
            if (t > best) best = t;
        }
        return best;
    };

    // How well a spot suits plants / stone, 0..1.
    auto floraSuit = [&](float a) {
        float s = (0.20f + 0.80f * moisture(a)) *
                  (1.0f - smooth(0.28f, 0.62f, slopeAt(a))) *
                  (1.0f - smooth(0.50f, 0.70f, altAt(a))) *
                  clampf(0.75f + hollowAt(a) / 40.0f, 0.45f, 1.5f);
        return clampf(s, 0.0f, 1.0f);
    };
    auto stoneSuit = [&](float a) {
        float s = 0.24f + 0.50f * richness(a) + 0.45f * smooth(0.18f, 0.55f, slopeAt(a)) +
                  0.35f * smooth(0.50f, 0.75f, altAt(a)) + 0.70f * talus(a);
        return clampf(s / 1.3f, 0.0f, 1.0f);
    };
    // A spot drawn in proportion to its suitability (raised to `sharp`: > 1
    // keeps to the best ground, < 1 wanders onto poorer ground too).
    auto pickSpot = [&](const auto& suit, float sharp, float& outSuit) {
        float a = 0.0f;
        for (int tries = 0; tries < 60; tries++) {
            a = rng.range(0.0f, 360.0f);
            outSuit = suit(a);
            if (rng.next01() < powf(outSuit, sharp)) return a;
        }
        return a;                                   // nowhere good: take the last look
    };

    // ---- Planting ---------------------------------------------------------------
    // (A twelfth of the budget is kept back for the stragglers at the end.)
    int total = (int)(around / 85.0f * growth / 1.35f * 0.92f);
    int wantFlora = (int)(total * look.floraDensity / growth + 0.5f);
    int wantStone = total - wantFlora;
    harvest.reserve(total + 16);

    struct Member { float along, depth, size; Kind kind; };
    std::vector<Member> clump;
    // A clump's place in depth: behind the walking line a little less often
    // than in front of it; never right on the line (that would fight with
    // whoever is standing there).
    struct Side { float lo, hi, center, spread; };
    auto pickSide = [&]() {
        if (rng.chance(0.42f)) return Side{-18.0f, -5.0f, rng.range(-15.0f, -8.0f), 3.5f};
        return Side{7.0f, 55.0f, rng.range(12.0f, 48.0f), rng.range(6.0f, 11.0f)};
    };
    // Add a member near (along, side.center), keeping clear of the others in
    // the clump by `room` so stones do not sit inside one another.
    auto place = [&](const Side& side, float spreadAlong, float room, float size, Kind kind, float pull) {
        for (int tries = 0; tries < 6; tries++) {
            float along = rng.gauss() * spreadAlong * pull;
            float depth = clampf(side.center + rng.gauss() * side.spread, side.lo, side.hi);
            bool clear = true;
            for (const Member& m : clump) {
                if (fabsf(m.along - along) < room && fabsf(m.depth - depth) < 7.0f) { clear = false; break; }
            }
            if (!clear && tries < 5) continue;
            clump.push_back({along, depth, size, kind});
            return along;
        }
        return 0.0f;
    };
    auto commit = [&](float a) {
        float arcPerDeg = at(idx(a)) * (PI / 180.0f);
        for (const Member& m : clump) {
            Harvestable h{};
            h.angle = std::fmod(a + m.along / arcPerDeg + 720.0f, 360.0f);
            h.depth = m.depth;
            h.size = m.size;
            h.phase = rng.range(0.0f, 6.2831853f);
            h.kind = m.kind;
            h.hp = 1.0f;       // set properly on first use (see nearfield.cpp)
            h.grow = 1.0f;
            h.regrowIn = 0.0f;
            harvest.push_back(h);
        }
        int n = (int)clump.size();
        clump.clear();
        return n;
    };

    // Plants.
    for (int placed = 0, guard = 0; placed < wantFlora && guard < wantFlora * 4; guard++) {
        float suit = 0.0f, roll = rng.next01();
        Side side = pickSide();
        if (roll < 0.26f) {
            // A loner, on poorer ground as readily as good.
            float a = pickSpot(floraSuit, 0.25f, suit);
            bool bulb = rng.chance(0.15f);
            place(side, 0.0f, 0.0f, bulb ? rng.range(22.0f, 30.0f) : rng.range(11.0f, 19.0f),
                  bulb ? Harvestable::Bulb : Harvestable::Grass, 1.0f);
            placed += commit(a);
        } else if (roll < 0.40f) {
            // A grove of bulb plants: a tall one and its lesser neighbors.
            float a = pickSpot(floraSuit, 1.3f, suit);
            int n = 2 + (int)(rng.next01() * 2.6f);
            float spread = rng.range(22.0f, 38.0f);
            for (int k = 0; k < n; k++)
                place(side, spread, 13.0f, k == 0 ? rng.range(31.0f, 37.0f) : rng.range(20.0f, 29.0f),
                      Harvestable::Bulb, k == 0 ? 0.25f : 1.0f);
            placed += commit(a);
        } else {
            // A patch of grass: bigger and thicker on better ground, its
            // tufts tallest in the middle; often with a bulb or two in it.
            float a = pickSpot(floraSuit, 1.0f, suit);
            int n = 4 + (int)(rng.next01() * 4.0f + suit * 4.0f);
            float spread = rng.range(26.0f, 52.0f) + n * 3.0f;
            for (int k = 0; k < n; k++) {
                float along = place(side, spread, 7.0f, 0.0f, Harvestable::Grass, 1.0f);
                float edge = clampf(fabsf(along) / (2.0f * spread), 0.0f, 1.0f);
                clump.back().size = (23.0f - 11.0f * edge) * rng.range(0.85f, 1.15f);
            }
            if (rng.chance(0.45f)) {
                int bulbs = 1 + (int)(rng.next01() * 2.2f);
                for (int k = 0; k < bulbs; k++)
                    place(side, spread, 12.0f, rng.range(23.0f, 34.0f), Harvestable::Bulb, 0.55f);
            }
            placed += commit(a);
        }
    }

    // Stone. Crystals stay about two in five of it, as before.
    int crystals = 0;
    for (int placed = 0, guard = 0; placed < wantStone && guard < wantStone * 4; guard++) {
        float suit = 0.0f, roll = rng.next01();
        Side side = pickSide();
        bool wantCrystal = placed > 0 && crystals < 0.40f * placed;   // crystals are short of their share
        if (roll < 0.25f) {
            // A loner.
            float a = pickSpot(stoneSuit, 0.3f, suit);
            bool crystal = rng.chance(wantCrystal ? 0.55f : 0.20f);
            place(side, 0.0f, 0.0f, crystal ? rng.range(16.0f, 24.0f) : rng.range(9.0f, 16.0f),
                  crystal ? Harvestable::Crystal : Harvestable::Rock, 1.0f);
            crystals += crystal ? 1 : 0;
            placed += commit(a);
        } else if (roll < (wantCrystal ? 0.70f : 0.45f)) {
            // A crystal cluster: a big one with smaller ones round its foot,
            // where the ground is rich.
            float a = pickSpot([&](float x) { return stoneSuit(x) * (0.35f + 0.65f * richness(x)); }, 1.2f, suit);
            int n = 2 + (int)(rng.next01() * 2.6f);
            float spread = rng.range(13.0f, 22.0f);
            for (int k = 0; k < n; k++)
                place(side, spread, 11.0f, k == 0 ? rng.range(25.0f, 32.0f) : rng.range(12.0f, 20.0f),
                      Harvestable::Crystal, k == 0 ? 0.2f : 1.0f);
            crystals += n;
            placed += commit(a);
        } else {
            // A boulder and its scatter of stones; sometimes crystals have
            // grown out of the pile.
            float a = pickSpot(stoneSuit, 1.2f, suit);
            int n = 1 + (int)(rng.next01() * 3.6f);
            float spread = rng.range(18.0f, 30.0f);
            place(side, spread, 12.0f, rng.range(15.0f, 20.0f), Harvestable::Rock, 0.2f);
            for (int k = 0; k < n; k++)
                place(side, spread, 9.0f, rng.range(6.0f, 10.5f), Harvestable::Rock, 1.0f);
            if (rng.chance(wantCrystal ? 0.50f : 0.22f)) {
                int c = 1 + (int)(rng.next01() * 1.8f);
                for (int k = 0; k < c; k++)
                    place(side, spread, 10.0f, rng.range(14.0f, 22.0f), Harvestable::Crystal, 0.6f);
                crystals += c;
            }
            placed += commit(a);
        }
    }

    // NearField finds what is on screen by searching this list by angle.
    auto byAngle = [](const Harvestable& x, const Harvestable& y) { return x.angle < y.angle; };
    std::sort(harvest.begin(), harvest.end(), byAngle);

    // Stragglers. Patchy is right, but screen after screen of nothing is
    // not: wherever the ground has been left bare for more than about a
    // screen's width, a lone tuft or stone or two goes in — whichever the
    // ground there would grow.
    const float BARE = 520.0f;
    float degPerUnit = 360.0f / around;
    size_t grown = harvest.size();
    for (size_t i = 0; i < grown; i++) {
        float a0 = harvest[i].angle;
        float a1 = i + 1 < grown ? harvest[i + 1].angle : harvest[0].angle + 360.0f;
        float gap = (a1 - a0) / degPerUnit;
        for (float x = rng.range(260.0f, BARE); x < gap - 200.0f; x += rng.range(260.0f, BARE)) {
            float a = std::fmod(a0 + x * degPerUnit, 360.0f);
            Side side = pickSide();
            bool plant = look.floraDensity > 0.0f && floraSuit(a) > 0.05f &&
                         (look.mineralDensity <= 0.0f || rng.chance(0.65f));
            if (plant) {
                place(side, 0.0f, 0.0f, rng.range(10.0f, 17.0f), Harvestable::Grass, 1.0f);
            } else {
                bool crystal = rng.chance(0.15f);
                place(side, 0.0f, 0.0f, crystal ? rng.range(14.0f, 20.0f) : rng.range(6.0f, 14.0f),
                      crystal ? Harvestable::Crystal : Harvestable::Rock, 1.0f);
                if (!crystal && rng.chance(0.4f))
                    place(side, 14.0f, 8.0f, rng.range(5.0f, 8.0f), Harvestable::Rock, 1.0f);
            }
            commit(a);
        }
    }
    std::sort(harvest.begin(), harvest.end(), byAngle);
}
