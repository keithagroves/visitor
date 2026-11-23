#include "trade.h"
#include <cmath>

namespace Trade {

namespace {

// Item ids, as App::inventory: 0 carbon, 1 ferrite, 2..8 the crystal of each
// kind of world (2 + Planet::archetype), 9 calcite, 10 glowcap, 11 gold.
const int CARBON = 0, FERRITE = 1, CRYSTAL0 = 2, CRYSTAL_END = 9, CALCITE = 9, GLOWCAP = 10, GOLD = 11;

// A stable 0..1 number for (post, thing).
float hash01(unsigned int seed, unsigned int salt) {
    unsigned int h = seed * 2654435761u ^ (salt + 0x9e3779b9u) * 2246822519u;
    h ^= h >> 15; h *= 2246822519u;
    h ^= h >> 13; h *= 3266489917u;
    h ^= h >> 16;
    return (h & 0xffffffu) * (1.0f / 16777216.0f);
}

bool isCrystal(int item) { return item >= CRYSTAL0 && item < CRYSTAL_END; }

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

struct UpgradeInfo { int levels; int price[4]; };
const UpgradeInfo kUpgrades[UPGRADES] = {
    {4, {200, 450, 900, 1600}},    // FuelTank
    {3, {150, 350, 750, 0}},       // Jetpack
    {3, {180, 420, 850, 0}},       // Laser
    {3, {220, 480, 950, 0}},       // Cutter
    {3, {250, 550, 1100, 0}},      // CargoBay
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

int maxLevel(int upgrade) {
    return upgrade >= 0 && upgrade < UPGRADES ? kUpgrades[upgrade].levels : 0;
}

int upgradePrice(int upgrade, int level) {
    if (upgrade < 0 || upgrade >= UPGRADES || level < 0 || level >= kUpgrades[upgrade].levels) return 0;
    return kUpgrades[upgrade].price[level];
}

float fuelScale(int level)    { return 1.0f + 0.30f * level; }
float jetpackScale(int level) { return 1.0f + 0.40f * level; }
float laserScale(int level)   { return 1.0f + 0.40f * level; }
float cutterScale(int level)  { return 1.0f + 0.35f * level; }
int   cargoBonus(int level)   { return level; }

int effect(int upgrade, int level) {
    switch (upgrade) {
        case FuelTank: return (int)lroundf((fuelScale(level) - 1.0f) * 100.0f);
        case Jetpack:  return (int)lroundf((jetpackScale(level) - 1.0f) * 100.0f);
        case Laser:    return (int)lroundf((laserScale(level) - 1.0f) * 100.0f);
        case Cutter:   return (int)lroundf((cutterScale(level) - 1.0f) * 100.0f);
        case CargoBay: return cargoBonus(level);
        default:       return 0;
    }
}

}  // namespace Trade
