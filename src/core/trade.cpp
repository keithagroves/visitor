#include "trade.h"
#include <algorithm>
#include <cmath>

namespace Trade {

namespace {


// A stable 0..1 number for (post, thing).
float hash01(unsigned int seed, unsigned int salt) {
    unsigned int h = seed * 2654435761u ^ (salt + 0x9e3779b9u) * 2246822519u;
    h ^= h >> 15; h *= 2246822519u;
    h ^= h >> 13; h *= 3266489917u;
    h ^= h >> 16;
    return (h & 0xffffffu) * (1.0f / 16777216.0f);
}


// How much this post wants an item: the multiplier on its base value.
float demand(const Planet& p, const Planet::Outpost& o, int item) {
    float u = hash01(o.seed, (unsigned int)item * 977u + 13u);
    if (item == COW) {
        // Cows graze on the home world (the one temperate world of a system):
        // nothing special there. Nowhere else has ever seen one.
        return p.archetype == 0 ? 1.0f : 1.5f + 0.5f * u;
    }
    float m = 0.85f + 0.40f * u;
    if (isCrystal(item)) {
        m = item == CRYSTAL0 + p.archetype ? 0.55f + 0.15f * u     // their own: everywhere underfoot
                                           : 1.45f + 0.75f * u;    // from another kind of world
    } else if (item == CARBON) {
        if (p.look.floraDensity <= 0.05f) m = 2.2f + 0.6f * u;     // nothing grows here
        else m *= 1.0f / (0.65f + 0.35f * (p.look.floraDensity > 1.8f ? 1.8f : p.look.floraDensity));
    } else if (item == GOLD) {
        m = 0.95f + 0.40f * u;
    }
    if (item == wanted(p, o)) m *= 1.4f;
    return m;
}

// Recipes name some materials by role; System says which crystal each is.
constexpr int HOME = -1, MOON = -2, FAR_A = -3, FAR_B = -4, EVERY = -5;

struct Step { int credits; Cost mats[3]; };
struct UpgradeInfo { int levels; Step step[3]; };
// The ladder (docs/gameplay.md). Tier 0: ferrite. Tier I: calcite, glowcap,
// the home crystal. Then the moon's crystal (Laser II, the scanner), gold
// (tier II) and the far crystals (tier III).
const UpgradeInfo kUpgrades[UPGRADES] = {
    // FuelTank
    {3, {{250,  {{FERRITE, 12}, {CALCITE, 8}, {HOME, 6}}},
         {700,  {{GOLD, 12}, {MOON, 10}, {0, 0}}},
         {1500, {{FAR_A, 12}, {GOLD, 15}, {0, 0}}}}},
    // CargoBay
    {3, {{200,  {{FERRITE, 15}, {CARBON, 10}, {0, 0}}},
         {600,  {{GOLD, 8}, {FERRITE, 15}, {0, 0}}},
         {1300, {{FAR_B, 12}, {GOLD, 10}, {0, 0}}}}},
    // StarDrive: ten of every kind of crystal in the system, and gold
    {1, {{3000, {{EVERY, 10}, {GOLD, 30}, {0, 0}}}}},
    // Seals
    {3, {{250,  {{HOME, 10}, {GLOWCAP, 6}, {0, 0}}},
         {700,  {{GOLD, 12}, {MOON, 10}, {0, 0}}},
         {1500, {{FAR_A, 15}, {FAR_B, 10}, {0, 0}}}}},
    // Pack
    {3, {{150,  {{CARBON, 10}, {CALCITE, 8}, {0, 0}}},
         {500,  {{GOLD, 8}, {MOON, 10}, {0, 0}}},
         {1200, {{FAR_A, 10}, {FAR_B, 10}, {0, 0}}}}},
    // Jetpack
    {3, {{150,  {{GLOWCAP, 8}, {FERRITE, 10}, {0, 0}}},
         {500,  {{GOLD, 8}, {MOON, 8}, {0, 0}}},
         {1100, {{FAR_B, 12}, {GOLD, 8}, {0, 0}}}}},
    // Cutter: the first thing you buy, then two levels of speed
    {3, {{150,  {{FERRITE, 10}, {0, 0}, {0, 0}}},
         {300,  {{CALCITE, 8}, {FERRITE, 10}, {0, 0}}},
         {800,  {{GOLD, 10}, {FERRITE, 10}, {0, 0}}}}},
    // Laser: II mines gold
    {2, {{400,  {{MOON, 12}, {FERRITE, 10}, {0, 0}}},
         {1500, {{FAR_B, 15}, {GOLD, 15}, {0, 0}}}}},
    // Scanner
    {1, {{350,  {{MOON, 10}, {GLOWCAP, 6}, {0, 0}}}}},
};

}  // namespace

int baseValue(int item) {
    switch (item) {
        case CARBON:  return 2;
        case FERRITE: return 4;
        case CALCITE: return 6;
        case GLOWCAP: return 14;
        case GOLD:    return 90;
        case COW:     return 150;
        default:      return isCrystal(item) ? 24 : 0;
    }
}

int wanted(const Planet& p, const Planet::Outpost& o) {
    // Anything but their own crystal (which they have plenty of).
    int own = CRYSTAL0 + p.archetype;
    int w = (int)(hash01(o.seed, 4242u) * ITEMS);
    if (w >= ITEMS) w = ITEMS - 1;
    if (w == own) w = (w + 1) % ITEMS;
    return w;
}

int sellPrice(const Planet& p, const Planet::Outpost& o, int item) {
    int base = baseValue(item);
    if (base <= 0) return 0;
    int v = (int)lroundf(base * demand(p, o, item));
    return v < 1 ? 1 : v;
}

int trend(const Planet& p, const Planet::Outpost& o, int item) {
    float m = demand(p, o, item);
    return m >= 1.35f ? 1 : (m <= 0.75f ? -1 : 0);
}

int goodsItem(const Planet& p, const Planet::Outpost& o, int slot) {
    if (slot == 0) return CRYSTAL0 + p.archetype;
    // The plain local thing they have most of to spare: whichever of these
    // they would pay least for.
    const int staples[4] = {CARBON, FERRITE, CALCITE, GLOWCAP};
    int best = FERRITE;
    float bestM = 1e9f;
    for (int s : staples) {
        if (s == CARBON && p.look.floraDensity <= 0.05f) continue;     // none to sell
        float m = demand(p, o, s);
        if (m < bestM) { bestM = m; best = s; }
    }
    return best;
}

int goodsPrice(const Planet& p, const Planet::Outpost& o, int slot) {
    return (int)lroundf(sellPrice(p, o, goodsItem(p, o, slot)) * 1.5f) + 1;
}

int goodsStock(Planet::Outpost& o, int slot) {
    if (slot < 0 || slot >= GOODS) return 0;
    if (o.stock[slot] < 0) {
        float u = hash01(o.seed, 9001u + (unsigned int)slot);
        o.stock[slot] = slot == 0 ? 10 + (int)(u * 15.0f) : 20 + (int)(u * 25.0f);
    }
    return o.stock[slot];
}

System systemOf(const std::vector<Planet>& planets, int homeIdx) {
    System sys;
    const Planet& home = planets[homeIdx];
    sys.home = crystalOf(home.archetype);
    sys.moon = crystalOf(6);
    auto addKind = [&](int item) {
        for (int k = 0; k < sys.kindCount; k++) if (sys.kinds[k] == item) return;
        if (sys.kindCount < 7) sys.kinds[sys.kindCount++] = item;
    };
    // The far kinds, nearest to the home orbit first.
    struct Far { int item; float gap; };
    std::vector<Far> far;
    for (const Planet& p : planets) {
        if (p.isSun) continue;
        int item = crystalOf(p.archetype);
        addKind(item);
        if (item == sys.home || item == sys.moon) continue;
        const Planet* top = &p;      // a moon counts at its planet's distance
        while (top->orbitParent >= 0 && !planets[top->orbitParent].isSun) top = &planets[top->orbitParent];
        float gap = fabsf(top->orbitRadius - home.orbitRadius);
        bool seen = false;
        for (Far& f : far) if (f.item == item) { f.gap = std::min(f.gap, gap); seen = true; }
        if (!seen) far.push_back({item, gap});
    }
    std::sort(far.begin(), far.end(), [](const Far& a, const Far& b) { return a.gap < b.gap; });
    sys.farA = far.size() > 0 ? far[0].item : sys.moon;
    sys.farB = far.size() > 1 ? far[1].item : sys.farA;
    std::sort(sys.kinds, sys.kinds + sys.kindCount);
    return sys;
}

int maxLevel(int upgrade) {
    return upgrade >= 0 && upgrade < UPGRADES ? kUpgrades[upgrade].levels : 0;
}

Recipe recipe(int upgrade, int level, const System& sys) {
    Recipe r;
    if (upgrade < 0 || upgrade >= UPGRADES || level < 0 || level >= kUpgrades[upgrade].levels) return r;
    const Step& st = kUpgrades[upgrade].step[level];
    r.credits = st.credits;
    auto add = [&](int item, int n) {
        for (int k = 0; k < r.matCount; k++)
            if (r.mats[k].item == item) { r.mats[k].count += n; return; }
        if (r.matCount < 8) r.mats[r.matCount++] = Cost{item, n};
    };
    for (const Cost& c : st.mats) {
        if (c.count <= 0) continue;
        switch (c.item) {
            case HOME:  add(sys.home, c.count); break;
            case MOON:  add(sys.moon, c.count); break;
            case FAR_A: add(sys.farA, c.count); break;
            case FAR_B: add(sys.farB, c.count); break;
            case EVERY: for (int k = 0; k < sys.kindCount; k++) add(sys.kinds[k], c.count); break;
            default:    add(c.item, c.count); break;
        }
    }
    return r;
}

// What the levels do. Tank sizes are tuned so the stock ship reaches the
// home world's moon and lands there with some to spare, but no other planet
// (see docs/gameplay.md).
float fuelScale(int level)    { static const float k[4] = {1.0f, 1.5f, 2.2f, 3.0f}; return k[std::clamp(level, 0, 3)]; }
int   holdSize(int level)     { return 150 + 100 * level; }
int   cowStalls(int level)    { return 3 + level; }
float suitScale(int level)    { return 1.0f + level; }
int   packSize(int level)     { static const int k[4] = {40, 70, 100, 140}; return k[std::clamp(level, 0, 3)]; }
float jetpackScale(int level) { return 1.0f + 0.40f * level; }
float cutterScale(int level)  { static const float k[4] = {0.0f, 1.0f, 1.35f, 1.75f}; return k[std::clamp(level, 0, 3)]; }
float laserScale(int level)   { static const float k[3] = {1.0f, 1.4f, 2.0f}; return k[std::clamp(level, 0, 2)]; }
bool  laserMinesGold(int level) { return level >= 1; }

int effect(int upgrade, int level) {
    auto pct = [](float x) { return (int)lroundf((x - 1.0f) * 100.0f); };
    switch (upgrade) {
        case FuelTank: return pct(fuelScale(level));
        case CargoBay: return holdSize(level);
        case Seals:    return (int)lroundf(suitScale(level));
        case Pack:     return packSize(level);
        case Jetpack:  return pct(jetpackScale(level));
        case Cutter:   return level <= 0 ? 0 : pct(cutterScale(level));
        case Laser:    return pct(laserScale(level));
        default:       return level;
    }
}

}  // namespace Trade
