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
// Upgrades. Sold at every post, same price everywhere; each has a few
// levels, each level dearer than the last.
namespace Trade {

constexpr int ITEMS = 12;        // inventory item ids (App::inventory)
constexpr int COW = 100;         // a cow from the ship's hold, as a thing to sell
constexpr int GOODS = 2;         // slots of goods a post sells (Outpost::stock)

enum Upgrade { FuelTank = 0, Jetpack, Laser, Cutter, CargoBay, UPGRADES };

// What one of an item is worth before a post's demand for it.
int baseValue(int item);
// What this post pays for one of an item (an inventory id, or COW).
int sellPrice(const Planet& p, const Planet::Outpost& o, int item);
// +1 = they pay well for it, -1 = they pay poorly, 0 = about what it is worth.
int trend(const Planet& p, const Planet::Outpost& o, int item);
// The one thing this post is short of (an inventory id).
int wanted(const Planet& p, const Planet::Outpost& o);

// The item a goods slot holds, what one costs, and (rolling it the first
// time) how many are left.
int goodsItem(const Planet& p, const Planet::Outpost& o, int slot);
int goodsPrice(const Planet& p, const Planet::Outpost& o, int slot);
int goodsStock(Planet::Outpost& o, int slot);

// Upgrades: how many levels there are, and what the next one costs when you
// have `level` of them (0 once it is at its maximum).
int maxLevel(int upgrade);
int upgradePrice(int upgrade, int level);
// What a level does.
float fuelScale(int level);      // x the ship's tank
float jetpackScale(int level);   // x the jetpack's tank
float laserScale(int level);     // x how fast the mining laser works
float cutterScale(int level);    // x how fast the terrain cutter bites
int   cargoBonus(int level);     // + cows the hold takes
// The same as one number for the panel: percent more (tanks, tools), or cows
// more (the cargo bay).
int   effect(int upgrade, int level);

}  // namespace Trade
