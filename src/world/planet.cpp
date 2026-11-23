#include "planet.h"
#include <cmath>
#include <algorithm>

// Crust tuning: the destructible shell spans [baseRadius - SHELL_DEPTH .. peaks];
// everything below is the always-solid core. CELL is the dig/mesh granularity.
static constexpr float SHELL_DEPTH = 9000.0f;
static constexpr float CELL = 250.0f;

namespace {
// Small deterministic RNG so each planet's terrain is stable for a given seed
// (stands in for the original's reliance on a globally seeded noise field).
struct Rng {
    unsigned int state;
    explicit Rng(unsigned int s) : state(s ? s : 1) {}
    float next01() {
        state = state * 1664525u + 1013904223u;
        return (state >> 8) * (1.0f / 16777216.0f); // 24-bit mantissa -> [0,1)
    }
    float range(float a, float b) { return a + next01() * (b - a); }
};
}

void Planet::setDensity(float d) {
    density = d;
    gravity = d * (baseRadius * baseRadius * baseRadius) / GRAVITY_REFERENCE_RADIUS;
}

void Planet::applyGlobalDensity(float globalDensity) {
    if (gmRef > 0.0f) {
        density = globalDensity;
        gravity = gmRef * (globalDensity / REFERENCE_DENSITY);
    } else {
        setDensity(densityRel * globalDensity);
    }
}

void Planet::generate(Vec2 c, float radius, float noiseIntensityIn,
                      int numPoints, float densityIn, unsigned int seed,
                      const TerrainStyle& style) {
    center = c;
    baseRadius = radius;
    noiseIntensity = noiseIntensityIn;
    this->seed = seed;
    setDensity(densityIn);

    Rng rng(seed);
    PerlinNoise perlin(seed);
    // Seamless noise around the silhouette: sample 2D Perlin on a circle of
    // radius `freq` so angle 0 and 360 meet exactly (the old linear walk left a
    // small cliff at the seam). Returns roughly [-1, 1].
    auto circNoise = [&perlin](float angleDeg, float freq, float ox, float oy) {
        float a = angleDeg * (3.14159265f / 180.0f);
        return perlin.noise(cosf(a) * freq + ox, sinf(a) * freq + oy);
    };

    // Per-octave decorrelation offsets, fixed for this planet's seed.
    float octOx[6], octOy[6];
    for (int o = 0; o < 6; o++) {
        octOx[o] = rng.range(-100.0f, 100.0f);
        octOy[o] = rng.range(-100.0f, 100.0f);
    }

    // Rolling fBm: gentle 3-octave hills for the basins. ~[0, 1].
    auto rollingFbm = [&](float angleDeg) {
        float sum = 0, amp = 1.0f, freq = 1.6f * style.rolling, norm = 0;
        for (int o = 0; o < 3; o++) {
            sum += (circNoise(angleDeg, freq, octOx[o], octOy[o]) * 0.5f + 0.5f) * amp;
            norm += amp;
            amp *= 0.5f;
            freq *= 2.0f;
        }
        return sum / norm;
    };

    // Ridged multifractal (Musgrave): fold the noise at its zero crossings
    // (1 - |n|) into sharp V-creases, square to pinch the summits, and weight
    // each octave by the one below it so detail piles onto crests while
    // valley floors stay smooth. This is the same construction the classic
    // 3D heightmap mountains use, evaluated around the circle. ~[0, 1].
    auto ridgedFbm = [&](float angleDeg) {
        float sum = 0, amp = 0.55f, freq = 2.2f, w = 1.0f, norm = 0;
        for (int o = 0; o < 5; o++) {
            float p = circNoise(angleDeg, freq, octOx[o], octOy[o]);
            float r = 1.0f - std::fabs(p) * 1.6f;
            if (r < 0) r = 0;
            r = r * r * w;
            w = r * 1.8f;
            if (w > 1) w = 1;
            sum += r * amp;
            norm += amp;
            amp *= 0.5f;
            freq *= 2.15f;
        }
        return sum / norm;
    };

    // 1-2 mountain "range" centers. Angles near these cluster more sample
    // points (sharper detail) and get taller peaks.
    int numRanges = (int)std::floor(rng.range((float)style.rangesMin, (float)style.rangesMax + 1.0f));
    if (numRanges > style.rangesMax) numRanges = style.rangesMax;
    struct Range { float center, width, intensity; };
    std::vector<Range> ranges;
    for (int i = 0; i < numRanges; i++) {
        ranges.push_back({rng.range(0.0f, 360.0f), rng.range(14.0f, 22.0f),
                          rng.range(2.5f, 4.0f) * style.rangeBoost});
    }

    // Impact craters (airless bodies): a parabolic bowl with a raised rim,
    // subtracted from the height profile. Angular half-widths are wide enough
    // to survive the silhouette's sample spacing.
    struct Crater { float center, halfWidth, depth; };
    std::vector<Crater> craters;
    for (int i = 0; i < style.craters; i++) {
        craters.push_back({rng.range(0.0f, 360.0f), rng.range(7.0f, 16.0f),
                           rng.range(0.35f, 0.8f)});
    }
    auto craterOffset = [&craters](float theta) {
        float off = 0;
        for (const auto& cr : craters) {
            float delta = std::fmod(theta - cr.center + 540.0f, 360.0f) - 180.0f;
            float x = std::fabs(delta) / cr.halfWidth;       // 1 at the rim
            if (x < 1.0f) off -= cr.depth * (1.0f - x * x);   // bowl
            float rim = (x - 1.0f) / 0.22f;
            off += cr.depth * 0.28f * std::exp(-rim * rim);   // raised lip
        }
        return off;
    };
    auto rangeWeight = [&ranges](float theta) {
        float w = 0;
        for (const auto& r : ranges) {
            float delta = std::fmod(theta - r.center + 540.0f, 360.0f) - 180.0f;
            w += r.intensity * std::exp(-(delta * delta) / (2.0f * r.width * r.width));
        }
        return w;
    };

    // Inverse-CDF angle sampling so clustered regions get more points while the
    // polygon stays monotonic in angle (can't self-intersect).
    const int BUCKETS = 720;
    std::vector<float> cumulative(BUCKETS + 1, 0.0f);
    for (int i = 0; i < BUCKETS; i++) {
        float theta = (i + 0.5f) * (360.0f / BUCKETS);
        cumulative[i + 1] = cumulative[i] + 1.0f + rangeWeight(theta);
    }
    float totalMass = cumulative[BUCKETS];
    auto angleAt = [&](float t) {
        float target = t * totalMass;
        int lo = 0, hi = BUCKETS;
        while (lo < hi) {
            int mid = (lo + hi) >> 1;
            if (cumulative[mid] < target) lo = mid + 1;
            else hi = mid;
        }
        int idx = lo > 0 ? lo - 1 : 0;
        float frac = (target - cumulative[idx]) / (cumulative[idx + 1] - cumulative[idx]);
        return (idx + frac) * (360.0f / BUCKETS);
    };

    // Height profile: rolling fBm in the basins, ridged multifractal inside the
    // range clusters (blended by rangeWeight), with the ranges also getting an
    // amplitude boost. Ridged peaks push altFrac toward 1, which is where the
    // body shader's snow band lives — so the high ranges cap with snow while
    // the basins stay in the dark tones.
    landscape.clear();
    for (int i = 0; i < numPoints; i++) {
        float angle = angleAt((float)i / numPoints);
        float w = rangeWeight(angle);
        float mountain = w / (w + 0.8f);   // 0 in basins -> ~1 at range centers
        float h = rollingFbm(angle) * (1.0f - mountain) + ridgedFbm(angle) * mountain;
        float amp = noiseIntensity * (1.0f + w * 0.6f);
        float r = baseRadius + h * amp + craterOffset(angle) * noiseIntensity;
        landscape.push_back({angle, r});
    }
    // Close the loop.
    if (!landscape.empty()) landscape.push_back(landscape.front());

    // Water. One level for the whole world, set where the wanted share of the
    // ground lies below it: the low ground then fills into a handful of
    // ponds, lakes and seas, whatever the terrain happens to offer. Its own
    // RNG, so a dry world (and everything else about a wet one) generates
    // exactly as before.
    sea = false;
    seaLevel = 0.0f;
    if (!isSun && atmosphere && style.seaMax > 0.0f && landscape.size() > 2) {
        Rng wrng(seed ^ 0x77617472u);
        bool wet = wrng.next01() < style.seaChance;
        float share = wrng.range(style.seaMin, style.seaMax);
        if (wet && share > 0.0f) {
            const int T = 4096;
            std::vector<float> hs(T);
            for (int k = 0; k < T; k++) hs[k] = seedRadiusBase(k * (360.0f / T)) - baseRadius;
            std::sort(hs.begin(), hs.end());
            int at = (int)(share * T);
            seaLevel = hs[at < 0 ? 0 : (at > T - 1 ? T - 1 : at)];
            sea = true;
            // The drop-in point is dry land: where the ground round it would
            // be under water (or awash), it is raised clear — fully at the
            // spot itself, easing off to nothing a few degrees out, so the
            // rise slopes back down into the water as a shore.
            if (style.keepClearHalf > 0.0f) {
                const float FULL = 1.6f, OUT = 4.5f, CLEAR = 80.0f;
                for (size_t i = 0; i + 1 < landscape.size(); i++) {
                    LandPoint& lp = landscape[i];
                    float d = std::fabs(std::fmod(lp.angle - style.keepClearAngle + 540.0f, 360.0f) - 180.0f);
                    if (d >= OUT) continue;
                    float t = d <= FULL ? 0.0f : (d - FULL) / (OUT - FULL);
                    float w = 1.0f - t * t * (3.0f - 2.0f * t);
                    float want = baseRadius + seaLevel + CLEAR;
                    if (lp.r < want) lp.r += (want - lp.r) * w;
                }
                landscape.back() = landscape.front();
            }
        }
    }

    // Mesas: flat-topped raised areas, a few hundred units across and a couple
    // of hundred tall — person-sized ground you climb onto and walk under (most
    // get a cave; see buildCaves). The terrain grid is 250 units a cell, so a
    // flank is never narrower than that: they read as steep, walkable slopes.
    // Their own RNG, so adding them leaves the rest of the world (ridges,
    // plants) exactly as it was. Not in mountain ranges (they'd sit on a
    // slope) and not on top of each other.
    mesas.clear();
    if (!isSun && style.mesaDensity > 0.0f) {
        Rng mrng(seed ^ 0x6d657361u);
        float arcPerDeg = baseRadius * (3.14159265f / 180.0f);
        int want = (int)(6.2831853f * baseRadius / 9000.0f * style.mesaDensity + 0.5f);
        for (int tries = 0; tries < want * 8 && (int)mesas.size() < want; tries++) {
            Mesa m;
            m.angle = mrng.range(0.0f, 360.0f);
            m.half = mrng.range(500.0f, 1000.0f);
            m.edge = mrng.range(230.0f, 320.0f);
            m.height = mrng.range(110.0f, 230.0f);
            float halfDeg = m.half / arcPerDeg;
            if (halfDeg > 40.0f) continue;                       // tiny moon: no room
            if (rangeWeight(m.angle) > 0.35f) continue;
            // Not in the water, nor with its feet in it: a mesa stands on dry
            // ground, so the caves that open in its flanks are dry too.
            if (sea) {
                bool awash = false;
                float reach = m.half + 200.0f;
                for (float x = -reach; x <= reach && !awash; x += 100.0f)
                    awash = seedRadiusBase(m.angle + x / arcPerDeg) < baseRadius + seaLevel + 35.0f;
                if (awash) continue;
            }
            if (style.keepClearHalf > 0.0f) {
                float d = std::fabs(std::fmod(m.angle - style.keepClearAngle + 540.0f, 360.0f) - 180.0f);
                if (d < style.keepClearHalf + halfDeg) continue;
            }
            bool clear = true;
            for (const Mesa& o : mesas) {
                float d = std::fabs(std::fmod(m.angle - o.angle + 540.0f, 360.0f) - 180.0f) * arcPerDeg;
                if (d < m.half + o.half + 600.0f) { clear = false; break; }
            }
            if (clear) mesas.push_back(m);
        }
    }

    // Background ridge layers (ported from planet.js generateBackgroundRidge):
    // uniform-angle silhouettes with smaller amplitude than the main terrain so
    // they mostly hide behind it, poking above only as distant horizon peaks.
    // Far layer first; tuning (follow/lift/alpha/skyMix) carried over 1:1.
    // Only worlds with air get hazy horizons (set atmosphere/isSun BEFORE
    // calling generate).
    ridges.clear();
    if (atmosphere && !isSun) {
        struct RidgeSpec {
            float ampScale, noiseStep, seedBias, follow, liftScale, alpha, skyMix;
        };
        // Far-to-near. Amplitudes/steps/haze carry the p5 tuning; the lifts are
        // raised from the original because our spawn terrain isn't flattened to
        // the valley floor like p5's landing pads were — without the lift the
        // ranges hide below the local ground and the horizon reads empty.
        //
        // Six BACKGROUND layers form a smooth atmospheric-perspective ramp
        // (Summer Afternoon-style): far ranges are pale, high-follow (slide
        // slowest), nearly dissolved into the sky haze; near ranges are
        // warm/dark, low-follow (slide faster), crisp. Stacking many planes at
        // a smooth depth gradient is what reads as a deep 3D scene rather than
        // a flat horizon. Tints lerp from look.ridgeFar to look.ridgeNear, so
        // every archetype gets its own range colors.
        //
        // follow: fraction of the player's angle the layer tracks. HIGHER =
        // moves less relative to the camera = reads farther away. So the far
        // hazy layer gets the biggest follow and the near layer the smallest.
        //
        // (The near framing plane that used to live here as two extra
        // world-space layers is now screen-anchored: see main's drawForeground.)
        const RidgeSpec specs[] = {
            {0.26f, 0.018f, 1700.0f, 0.91f, 0.58f, 110.0f / 255.0f, 0.93f},
            {0.33f, 0.024f, 1400.0f, 0.84f, 0.48f, 135.0f / 255.0f, 0.85f},
            {0.43f, 0.032f, 1150.0f, 0.76f, 0.39f, 165.0f / 255.0f, 0.74f},
            {0.55f, 0.041f,  900.0f, 0.67f, 0.30f, 195.0f / 255.0f, 0.61f},
            {0.68f, 0.050f,  680.0f, 0.58f, 0.22f, 222.0f / 255.0f, 0.47f},
            {0.84f, 0.058f,  460.0f, 0.49f, 0.15f, 242.0f / 255.0f, 0.34f},
        };
        const int specCount = (int)(sizeof(specs) / sizeof(specs[0]));
        int specIdx = 0;
        for (const RidgeSpec& s : specs) {
            Ridge layer;
            float depthT = specCount > 1 ? (float)specIdx / (specCount - 1) : 0.0f;  // 0 far .. 1 near
            specIdx++;
            layer.follow = s.follow;
            layer.lift = noiseIntensity * s.liftScale;
            layer.alpha = s.alpha;
            layer.skyMix = s.skyMix;
            layer.tintR = look.ridgeFar[0] + (look.ridgeNear[0] - look.ridgeFar[0]) * depthT;
            layer.tintG = look.ridgeFar[1] + (look.ridgeNear[1] - look.ridgeFar[1]) * depthT;
            layer.tintB = look.ridgeFar[2] + (look.ridgeNear[2] - look.ridgeFar[2]) * depthT;
            // 3x the main terrain's resolution: finer silhouette detail without
            // affecting collision/landing (which read the field LUT).
            int count = numPoints * 3;
            float amplitude = noiseIntensity * s.ampScale;
            // Match the old walk's bump count: it covered count*noiseStep noise
            // units per revolution; the circle sampler covers 2*pi*freq.
            float baseFreq = count * s.noiseStep / 6.2831853f;
            float lOx[3], lOy[3];
            for (int o = 0; o < 3; o++) {
                lOx[o] = rng.range(-100.0f, 100.0f) + s.seedBias;
                lOy[o] = rng.range(-100.0f, 100.0f);
            }
            layer.points.reserve(count + 1);
            for (int i = 0; i < count; i++) {
                float angle = i * (360.0f / count);
                // 3-octave ridged fBm so the horizon reads as peaks, not swells.
                float sum = 0, amp = 1.0f, freq = baseFreq, wgt = 1.0f, norm = 0;
                for (int o = 0; o < 3; o++) {
                    float p = circNoise(angle, freq, lOx[o], lOy[o]);
                    float r = 1.0f - std::fabs(p) * 1.6f;
                    if (r < 0) r = 0;
                    r = r * r * wgt;
                    wgt = r * 1.8f;
                    if (wgt > 1) wgt = 1;
                    sum += r * amp;
                    norm += amp;
                    amp *= 0.5f;
                    freq *= 2.1f;
                }
                layer.points.push_back({angle, baseRadius + (sum / norm) * amplitude});
            }
            layer.points.push_back(layer.points.front());
            layer.topR = baseRadius + layer.lift;
            for (const LandPoint& pt : layer.points)
                if (pt.r + layer.lift > layer.topR) layer.topR = pt.r + layer.lift;
            ridges.push_back(std::move(layer));
        }
    }

    // Harvestables: the plants and minerals on the ground strip around the
    // walking line (harvest_gen.cpp).
    growHarvest();

    // The locals and their trading posts (outposts.cpp).
    foundOutposts(style);

    // The destructible field is LAZY (built on approach via ensureField); until
    // then the surface LUT comes straight from the analytic silhouette.
    fieldReady_ = false;
    digOps_.clear();
    cavesBuilt_ = false;
    naturalOps_ = 0;
    caveProps.clear();
    caveMouths.clear();
    ores.clear();
    oreSeen.clear();
    rebuildSurfaceLUT();
}

void Planet::ensureField() {
    if (fieldReady_) return;
    float coreR = baseRadius - SHELL_DEPTH;
    if (coreR < baseRadius * 0.25f) coreR = baseRadius * 0.25f;
    float outerR = maxSurfaceRadius() + CELL * 2.0f;
    const int SEED_N = 4096;
    std::vector<float> seedLUT(SEED_N);
    for (int k = 0; k < SEED_N; k++) seedLUT[k] = seedRadius(k * (360.0f / SEED_N));
    field.generate(center, coreR, outerR, CELL, seedLUT);
    fieldReady_ = true;
    rebuildSurfaceLUT();
    if (!cavesBuilt_) buildCaves();
}

void Planet::releaseField() {
    if (!fieldReady_) return;
    field.release();
    carve.release();          // rebuilt from digOps_ when the player is back
    fieldReady_ = false;
    rebuildSurfaceLUT();   // falls back to the analytic silhouette + nothing else
}

void Planet::rebuildSurfaceLUT() {
    surfaceLUT.resize(LUT_N);
    if (fieldReady_) {
        for (int k = 0; k < LUT_N; k++)
            surfaceLUT[k] = field.outerSurfaceRadius(k * (360.0f / LUT_N));
    } else {
        for (int k = 0; k < LUT_N; k++)
            surfaceLUT[k] = seedRadius(k * (360.0f / LUT_N));
    }
}

float Planet::terrainAt(Vec2 world) const {
    if (fieldReady_) return field.density(world);
    // No grid loaded: a radial distance to the silhouette stands in.
    float dx = world.x - center.x, dy = world.y - center.y;
    return surfaceRadius(atan2d(dy, dx)) - sqrtf(dx * dx + dy * dy);
}

CarveMap::Terrain Planet::terrainLocal() const {
    return [this](Vec2 local) { return terrainAt(Vec2(center.x + local.x, center.y + local.y)); };
}

// The body shader's value noise (kBodyFrag hash/vnoise/fbm), ported as is.
// Exact agreement only matters right at a band edge.
static float shHash(float x, float y) {
    float v = sinf(x * 127.1f + y * 311.7f) * 43758.5453f;
    return v - floorf(v);
}
static float shNoise(float x, float y) {
    float ix = floorf(x), iy = floorf(y), fx = x - ix, fy = y - iy;
    float ux = fx * fx * (3.0f - 2.0f * fx), uy = fy * fy * (3.0f - 2.0f * fy);
    float a = shHash(ix, iy), b = shHash(ix + 1.0f, iy);
    float c = shHash(ix, iy + 1.0f), d = shHash(ix + 1.0f, iy + 1.0f);
    float top = a + (b - a) * ux, bot = c + (d - c) * ux;
    return top + (bot - top) * uy;
}
static float shFbm(float x, float y) {
    float v = 0.0f, a = 0.5f;
    for (int i = 0; i < 4; i++) { v += a * shNoise(x, y); x *= 2.1f; y *= 2.1f; a *= 0.5f; }
    return v;
}

void Planet::groundTone(Vec2 world, float out[3]) const {
    float lx = world.x - center.x, ly = world.y - center.y;
    float d = sqrtf(lx * lx + ly * ly);
    float n = shFbm(lx * 0.00005f, ly * 0.00005f);
    float band = noiseIntensity > 1.0f ? noiseIntensity : 1.0f;
    float alt = (d - baseRadius) / band;
    alt = alt < 0.0f ? 0.0f : (alt > 1.0f ? 1.0f : alt);
    float ragged = alt + (n - 0.5f) * 0.30f;
    // Same edges as the shader: basin | 0.18 | 0.44 | 0.62 | 0.78 | cap.
    int k = ragged < 0.18f ? 0 : ragged < 0.44f ? 1 : ragged < 0.62f ? 2 : ragged < 0.78f ? 3 : 4;
    for (int c = 0; c < 3; c++) out[c] = look.tones[k][c];
}

float Planet::rockAt(Vec2 world) const {
    float d = terrainAt(world);
    if (carve.ready()) {
        float c = carve.sample(Vec2(world.x - center.x, world.y - center.y));
        if (c < d) d = c;
    }
    return d;
}

Vec2 Planet::normalAt(Vec2 world) const {
    // Density rises into the rock, so the outward normal is minus its gradient.
    const float e = 3.0f;
    float gx = rockAt(Vec2(world.x + e, world.y)) - rockAt(Vec2(world.x - e, world.y));
    float gy = rockAt(Vec2(world.x, world.y + e)) - rockAt(Vec2(world.x, world.y - e));
    float len = sqrtf(gx * gx + gy * gy);
    if (len < 1e-5f) return (world - center).normalize();   // flat spot: straight up
    return Vec2(-gx / len, -gy / len);
}

bool Planet::focusCarve(Vec2 world) {
    Vec2 local(world.x - center.x, world.y - center.y);
    float quarter = 0.25f * (CarveMap::N - 1) * CarveMap::CELL;
    if (carve.ready()) {
        Vec2 c = carve.center();
        if (fabsf(local.x - c.x) < quarter && fabsf(local.y - c.y) < quarter) return false;
    }
    carve.setSea(seaRadius());
    carve.recenter(local, digOps_, terrainLocal());
    carve.markWetOps(digOps_);
    return true;
}

float Planet::visibleRadius(float angleDeg) const {
    float r = surfaceRadius(angleDeg);
    float s = seaRadius();
    return s > r ? s : r;
}

float Planet::waterDepth(Vec2 world) const {
    if (!sea) return 0.0f;
    Vec2 local(world.x - center.x, world.y - center.y);
    float depth = seaRadius() - local.length();
    if (depth <= 0.0f) return 0.0f;
    // Open above the ground and under the level: the sea itself.
    if (terrainAt(world) <= 0.0f) return depth;
    // In what was rock: only where it has been dug and the water has got in.
    if (carve.ready() && carve.sample(local) < 0.0f && carve.wetAt(local)) return depth;
    return 0.0f;
}

CarveMap::Rect Planet::dig(Vec2 world, float radius) {
    CarveMap::Op op{Vec2(world.x - center.x, world.y - center.y), radius};
    // Holding the cutter on one spot adds nothing after the first bite.
    if (!digOps_.empty()) {
        const CarveMap::Op& last = digOps_.back();
        if (radius <= last.radius && dist(last.local.x, last.local.y, op.local.x, op.local.y) < 1.0f)
            return CarveMap::Rect{};
    }
    digOps_.push_back(op);
    carve.setSea(seaRadius());
    CarveMap::Rect rc = carve.apply(op, terrainLocal());
    // Water that got in may have run on into older tunnels.
    carve.markWetOps(digOps_);
    return rc;
}

void Planet::setKeplerSpeed(float parentGravity) {
    if (orbitRadius <= 1.0f || parentGravity <= 0.0f) { orbitSpeed = 0.0f; return; }
    float w = sqrtf(parentGravity / (orbitRadius * orbitRadius * orbitRadius));   // rad per frame-unit
    orbitSpeed = w * (180.0f / 3.14159265f);
}

void Planet::setOrbit(int parentIdx, float radius, float speedDegPerFrame, float startAngleDeg) {
    orbitParent = parentIdx;
    orbitRadius = radius;
    orbitSpeed = speedDegPerFrame;
    orbitAngle = startAngleDeg;
}

void Planet::updateOrbit(const std::vector<Planet>& planets, float timeScale) {
    if (orbitParent < 0 || orbitParent >= (int)planets.size()) {
        orbitVel = Vec2(0, 0);
        orbitStep = Vec2(0, 0);
        return;
    }
    const Planet& parent = planets[orbitParent];
    Vec2 oldCenter = center;
    if (!orbitPinned) orbitAngle += orbitSpeed * timeScale;
    if (orbitAngle > 360.0f) orbitAngle -= 360.0f;
    if (orbitAngle < -360.0f) orbitAngle += 360.0f;
    float ca = cosd(orbitAngle), sa = sind(orbitAngle);
    center = Vec2(parent.center.x + orbitRadius * ca, parent.center.y + orbitRadius * sa);
    // Analytic velocity (parent's motion + this orbit's tangential speed), not
    // a frame difference: it is valid on the very first frame, which is what
    // lets a freshly spawned ship start out co-moving with its planet.
    float tangential = orbitPinned ? 0.0f : orbitSpeed * DEG2RAD * orbitRadius;
    orbitVel = Vec2(parent.orbitVel.x - sa * tangential, parent.orbitVel.y + ca * tangential);
    // The exact move this frame — what things standing on the surface are
    // carried by (velocity * dt would drift off the circle over minutes).
    orbitStep = center - oldCenter;
    if (fieldReady_) field.moveTo(center);   // grid is center-relative; pure shift
}

float Planet::surfaceRadius(float angle) const {
    int n = (int)surfaceLUT.size();
    if (n == 0) return baseRadius;
    while (angle < 0.0f) angle += 360.0f;
    while (angle >= 360.0f) angle -= 360.0f;
    float f = angle / 360.0f * n;
    int i0 = (int)f % n;
    int i1 = (i0 + 1) % n;
    float frac = f - (float)((int)f);
    return surfaceLUT[i0] * (1.0f - frac) + surfaceLUT[i1] * frac;
}

float Planet::mesaOffset(float angle) const {
    float off = 0.0f;
    float arcPerDeg = baseRadius * (3.14159265f / 180.0f);
    for (const Mesa& m : mesas) {
        float x = std::fabs(std::fmod(angle - m.angle + 540.0f, 360.0f) - 180.0f) * arcPerDeg;
        if (x >= m.half) continue;
        float t = (m.half - x) / m.edge;             // 0 at the foot, 1 at the top's rim
        if (t > 1.0f) t = 1.0f;
        off += m.height * t * t * (3.0f - 2.0f * t);
    }
    return off;
}

float Planet::seedRadius(float angle) const {
    return seedRadiusBase(angle) + mesaOffset(angle);
}

float Planet::seedRadiusBase(float angle) const {
    if (landscape.size() < 2) return baseRadius;
    if (angle < 0) angle += 360.0f;
    for (size_t i = 0; i + 1 < landscape.size(); i++) {
        const LandPoint& cur = landscape[i];
        const LandPoint& next = landscape[i + 1];
        float curA = cur.angle;
        float nextA = next.angle < curA ? next.angle + 360.0f : next.angle;
        float adjA = angle < curA ? angle + 360.0f : angle;
        if (adjA >= curA && adjA <= nextA) {
            float f = (adjA - curA) / (nextA - curA);
            return cur.r + (next.r - cur.r) * f;
        }
    }
    return baseRadius;
}

float Planet::maxSurfaceRadius() const {
    float maxR = baseRadius;
    for (const auto& p : landscape) {
        if (p.r > maxR) maxR = p.r;
    }
    float tallest = 0.0f;
    for (const Mesa& m : mesas) tallest = std::max(tallest, m.height);
    return maxR + tallest;
}

float Planet::atmosphericDensity(float x, float y) const {
    if (!atmosphere) return 0.0f;
    float dx = x - center.x;
    float dy = y - center.y;
    float d = sqrtf(dx * dx + dy * dy);
    float altitude = d - baseRadius;
    float thickness = atmosphereThickness();
    if (thickness <= 0.0f || altitude >= thickness) return 0.0f;
    if (altitude <= 0) return 1.0f;
    return 1.0f - altitude / thickness;
}
