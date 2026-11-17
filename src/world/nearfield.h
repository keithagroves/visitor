#pragma once

#include "planet.h"
#include "../graphics/renderer2d.h"

// The near field: the strip of ground around the walking line and the
// harvestable plants and minerals standing on it (Planet::harvest).
//
// The game is a flat cross-section, but the ground is drawn as if seen from
// ~40 degrees above (the ground-ellipse rule in docs/art-style.md): the terrain
// silhouette is the FAR edge of the visible ground, and the walking line — where
// the ship, the astronaut and the cows stand — sits a little below it. This
// module extends that into a small third dimension. Every harvestable has a
// `depth`: how far in front of (+) or behind (-) the walking line it stands.
// Depth becomes
//   - a place on screen: further down for nearer things (0.64 of the depth,
//     i.e. sin 40 degrees), further up toward the silhouette for farther ones;
//   - a size: nearer is bigger;
//   - a tone: nearer is darker, farther hazier;
//   - a draw order: behind the walker, or over them.
// base()/center()/radius() are the single source of truth for where an item
// IS, so drawing, cursor picking and the laser's end point always agree.
//
// Depth is NOT a parallax. Everything here stands on the one ground profile
// the game draws, so an item that slid sideways faster than that ground (as
// nearer things would through a real camera) slid ALONG it — and where the
// ground slopes, up and down it: a crystal riding a mesa's flank like an
// escalator as you walked by. Things stay rooted where they stand.
namespace NearField {

// Everything that stands ON the walking line is drawn this far below the
// terrain line (it is groundDrop(0)): the astronaut's boots, and — so that
// feet meet floors — whatever has been dug there. See the body shader.
constexpr float WALK_DROP = 13.0f;
// A hole's mouth is drawn as an ellipse lying on the walking line, the way a
// round hole in the ground looks from a low angle. Its near rim — where the
// ground in front of the hole begins and covers it — sags this far below the
// walking line at the middle of the hole...
constexpr float MOUTH = 9.0f;
// ...its far rim arches this far above it...
constexpr float MOUTH_RISE = 8.0f;
// ...and both take this far in from the hole's wall to round off (a quarter
// circle), so the rims meet the sides of the hole in a curve, not a corner.
constexpr float MOUTH_ROUND = 14.0f;

// How far in front of the walking line the ground strip extends (the deepest
// a harvestable is ever placed). groundDrop(FRONT_DEPTH) is the strip's near
// edge on screen: below it you are looking at the open cross-section.
constexpr float FRONT_DEPTH = 55.0f;

// World units below the terrain silhouette line for a given depth. Depth 0 is
// the walking line (13 below the silhouette, where the blob shadows sit).
float groundDrop(float depth);
// Depth at a given distance below the silhouette (inverse of groundDrop).
float depthAtDrop(float drop);
// Apparent size multiplier: nearer is bigger.
float scale(float depth);

// What the functions need to know about the current view.
struct Frame {
    float viewAngle = 0;     // angle of the camera focus around the planet, degrees
    float halfSpanDeg = 10;  // how far either side of that is on screen
    float time = 0;          // animation clock
    Vec2 toSun;              // unit vector, planet -> star
    float strength = 1;      // surface-dressing fade (0 = don't draw)
    float light[3] = {1, 1, 1};  // day/dusk/night light on the strip
};

// Where a harvestable is, in world space, as drawn.
Vec2 base(const Planet& p, const Planet::Harvestable& h);
Vec2 center(const Planet& p, const Planet::Harvestable& h);
float radius(const Planet::Harvestable& h);

// Harvest rules. Resources: 0 = carbon (plants), 1 = ferrite (rock),
// 2 = crystal.
struct Yield { int resource; int amount; };
Yield yield(const Planet::Harvestable& h);          // by kind, and less from the small ones
float toughness(Planet::Harvestable::Kind kind);   // beam-seconds to cut one
bool alive(const Planet::Harvestable& h);
const float* color(const Planet& p, Planet::Harvestable::Kind kind);   // its resource color

// Draw the items behind the walking line (front = false) or in front of it
// (front = true). Must be inside an active world-space renderer batch. `hot`
// is the index currently under the laser (-1 = none).
void draw(Renderer2D& r, const Planet& p, const Frame& f, bool front, int hot);

// Index of the live harvestable under `cursor` and within `range` of `from`
// (both world space), or -1. `slack` widens every item's pick radius.
int pick(const Planet& p, const Frame& f, Vec2 cursor, Vec2 from, float range, float slack);

// Regrowth. Call once per frame for the world the player is on. `cutting` is
// the index under the laser right now (-1 = none): a half-cut item recovers
// when left alone, but not while the beam is still on it.
void update(Planet& p, float dtSeconds, int cutting);

// Cut a harvestable down: it vanishes and will grow back later.
void harvest(Planet::Harvestable& h);

// A circular bite has just been cut at a world point, on the walking line.
// Whatever stood on ground that is now gone goes with it (no yield): only
// where the bite actually breaks the surface, and only things within its reach
// in DEPTH as well as along the ground — a plant standing well in front of the
// walking line keeps its ground when a shaft goes down behind it. Returns how
// many were removed.
int uproot(Planet& p, const Frame& f, Vec2 world, float radius);

}  // namespace NearField
