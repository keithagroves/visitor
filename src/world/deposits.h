#pragma once

#include "planet.h"
#include "cavefield.h"
#include "nearfield.h"
#include "../graphics/renderer2d.h"

// Ore buried in the ground: clumps of ferrite, the world's crystal and gold
// through the rock under every rocky world, invisible until digging exposes
// them (Planet::ores).
//
// Where they are. Clumps are seeded on a grid of CELL-sized squares,
// planet-local: each square's contents (a short vein of a few clumps, or
// nothing) follow from the planet's seed and the square's coordinates alone,
// worked out when something asks. So there is nothing to generate up front
// and no limit to how deep they go; a planet only stores the clumps found so
// far and the ids of the ones it has looked at (Planet::oreSeen). What a vein
// is made of depends on its depth: ferrite from a little way down, the
// world's crystal from ~80, gold from ~150 — the depth where a hole reaches
// its darkest shade, so gold glints out of the dark — weighted by the kind of
// world. Long stretches of ground are rich, others barren.
//
// How they are found. Ore stops the terrain cutter: a bite that would cut
// into a clump is refused (it skips off in sparks), and the clump is revealed
// — left standing in the tunnel wall. A bite that brings dug space within
// REVEAL_GAP of a clump reveals it too. When a bite lands near a clump that
// is still hidden, some of its rock chips glitter in the ore's color: you
// are close. Clumps that a natural cave's walls already run past show from
// the start; ones a cave has hollowed out don't exist.
//
// How they are mined. A clump holds as many pieces as it yields — gold
// nuggets, ferrite cubes, crystal shards: what you see is what you get — and
// the mining laser takes them one at a time, each flying to the player as it
// comes loose. What has been taken stays taken. With the last piece the lump
// crumbles and leaves a pocket dug out of the rock where it was, which shows
// the next clump of its vein.
//
// Or destroyed. The cutter held on a clump heats it instead (Ore::heat): it
// turns red, then orange, and after burnSeconds — a few times longer than
// mining it would take — burns away to nothing, pocket and all. Nothing is
// collected. Left alone before that, it cools again.
//
// Like the caves' contents, clumps are drawn WALK_DROP below where they
// physically are, and only for a player who is underground.
namespace Deposits {

constexpr float CELL = 150.0f;           // the seeding grid, world units
constexpr float REVEAL_GAP = 4.0f;       // a clump shows once dug space is this close to its edge
constexpr float BLOCK_OVERLAP = 2.0f;    // the deepest a cutter bite may cut into one
constexpr float GLITTER_RANGE = 40.0f;   // a bite this close to a hidden clump throws glittering chips
constexpr float POCKET = 4.0f;           // a mined clump's pocket is this much wider than it was

// Where a clump is, in world space, as drawn.
Vec2 center(const Planet& p, const Planet::Ore& o);
float radius(const Planet::Ore& o);

// Mining rules. Resources as NearField/CaveField: 1 = ferrite, 2 = the
// world's crystal, 5 = gold.
NearField::Yield yield(const Planet::Ore& o);
float toughness(Planet::Ore::Kind kind);     // beam-seconds to mine one
void color(const Planet& p, Planet::Ore::Kind kind, float out[3]);
// The pieces of a clump: how many it holds (its yield), how many the laser
// has taken so far (they go in order, 0 first), and where piece k is — world
// space, as drawn — for the beam and the bits that fly off it.
int pieces(const Planet::Ore& o);
int taken(const Planet::Ore& o);
Vec2 pieceCenter(const Planet& p, const Planet::Ore& o, int k);
// Cutter-seconds to burn a clump away (toughness x the oreBurn knob).
float burnSeconds(Planet::Ore::Kind kind);

// ---- The terrain cutter ---------------------------------------------------
// The clump a bite (a circle, world space) would cut into, as an index into
// p.ores, or -1. One still hidden is revealed first (appended to p.ores).
int blocking(Planet& p, Vec2 world, float radius);
// After a dig: reveal the hidden clumps it brought dug space up to. Returns
// how many it found (the last entries of p.ores).
int revealNear(Planet& p, Vec2 world, float radius);
// The nearest hidden clump within GLITTER_RANGE of a bite's edge: how close
// it is (0 = at the range, 1 = right there) and its kind. 0 when none.
float glitter(const Planet& p, Vec2 world, float radius, Planet::Ore::Kind* kind);
// The fine carve window was just rebuilt: reveal the clumps it shows already
// (in the walls of a natural cave) and mark the ones a cave has hollowed out.
// Returns how many it revealed.
int scanWindow(Planet& p);

// ---- The mining laser -------------------------------------------------------
// Index of the revealed clump under `cursor`, within `range` of `from`, that
// the tool at `eye` (a physical point) has a clear line to — or -1.
int pick(const Planet& p, const CaveField::Frame& f, Vec2 cursor, Vec2 from, Vec2 eye, float range, float slack);
// Per-frame upkeep: a clump the cutter has heated cools when left alone
// (`burning` = the one under the cutter now, -1 = none).
void update(Planet& p, float dt, int burning);
// Take a clump out — mined or burnt: gone for good, its pocket dug out of
// the rock (returned for the texture upload), and any neighbours the pocket
// reaches revealed.
CarveMap::Rect extract(Planet& p, int idx);

// Draw the revealed clumps. Inside an active world-space batch, after the
// body pass (they sit in the rock, over it) and before the front-ground veil.
// `hot` is the index under the mining laser, `burning` the one under the
// cutter (-1 = none).
void draw(Renderer2D& r, const Planet& p, const CaveField::Frame& f, int hot, int burning);

// Dev: reveal every clump within `range` of a world point. Returns how many.
int revealAll(Planet& p, Vec2 world, float range);

}  // namespace Deposits
