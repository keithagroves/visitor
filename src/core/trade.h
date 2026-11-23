#pragma once

#include "../world/planet.h"

// Trading with the locals: what a post pays for the things you carry, what it
// sells, and what the upgrades cost and do. Pure rules — the money, the
// inventory and the upgrade levels themselves live in App (main.cpp), the
// panel in shell.html.
//
// Prices. Every item has a base value. A post pays that times its own demand
// for the item, which follows from the post's seed and the kind of world it
// is on:
//   - a world's own crystal is common there and fetches little; crystals from
//     any other kind of world are exotic and fetch about three times as much
//     — so what you mine on one world is worth carrying to the next;
//   - plant matter is dear where nothing grows;
//   - each post is short of one thing in particular and pays extra for it;
//   - cows are worth more anywhere that has never seen one.
// Goods. A post sells what it has plenty of — its world's crystal and one
// other local thing — at half as much again as it would pay for them, from a
// stock that does not refill. Buy low, fly, sell high.
// Equipment. Every trader builds every upgrade, from materials plus credits
// (docs/gameplay.md): each tier's key material comes from the next place out,
// so credits alone never buy past the first tier. Recipes name materials by
// role — the home world's crystal, the moon's, the two "far" kinds from the
// system's other planets — and System resolves the roles to item ids for the
// system being played, so every system is winnable.
namespace Trade {

constexpr int ITEMS = 12;        // inventory item ids (App::pack / App::hold)
constexpr int COW = 100;         // a cow from the ship's hold, as a thing to sell
constexpr int GOODS = 2;         // slots of goods a post sells (Outpost::stock)

// Item ids.
constexpr int CARBON = 0, FERRITE = 1, CRYSTAL0 = 2, CALCITE = 9, GLOWCAP = 10, GOLD = 11;
inline int crystalOf(int archetype) { return CRYSTAL0 + archetype; }
inline bool isCrystal(int item) { return item >= CRYSTAL0 && item < CRYSTAL0 + 7; }

enum Upgrade {
    FuelTank = 0, CargoBay, StarDrive,     // ship
    Seals, Pack, Jetpack,                  // suit
    Cutter, Laser, Scanner,                // tool
    UPGRADES
};

// The crystals of the system being played, by role.
struct System {
    int home = CRYSTAL0;             // the home world's (temperate)
    int moon = CRYSTAL0 + 6;         // the moon's (barren)
    int farA = -1, farB = -1;        // the two nearest other kinds, nearest first
    int kinds[7] = {0};              // item ids of every kind in the system...
    int kindCount = 0;               // ...and how many
};
// Work it out from the generated bodies (homeIdx = the home world).
System systemOf(const std::vector<Planet>& planets, int homeIdx);

// What one level of an upgrade costs.
struct Cost { int item; int count; };
struct Recipe {
    int credits = 0;
    Cost mats[8];
    int matCount = 0;
};

// What a post pays and sells: see the top of this file.
int baseValue(int item);
int sellPrice(const Planet& p, const Planet::Outpost& o, int item);
int trend(const Planet& p, const Planet::Outpost& o, int item);
int wanted(const Planet& p, const Planet::Outpost& o);
int goodsItem(const Planet& p, const Planet::Outpost& o, int slot);
int goodsPrice(const Planet& p, const Planet::Outpost& o, int slot);
int goodsStock(Planet::Outpost& o, int slot);

// Upgrades: how many levels there are, and the recipe for the next one when
// you have `level` of them (credits 0 and no materials past the top).
int maxLevel(int upgrade);
Recipe recipe(int upgrade, int level, const System& sys);

// What a level does.
float fuelScale(int level);      // x the ship's tank
int   holdSize(int level);       // items the ship's hold takes
int   cowStalls(int level);      // cows the hold takes
float suitScale(int level);      // x how long the suit lasts where it drains
int   packSize(int level);       // items the suit carries
float jetpackScale(int level);   // x the jetpack's tank
float cutterScale(int level);    // x how fast the terrain cutter bites (level 0 = no cutter)
float laserScale(int level);     // x how fast the mining laser works
bool  laserMinesGold(int level);
// The same as one number for the panel (percent, items, cows, x).
int   effect(int upgrade, int level);

}  // namespace Trade
