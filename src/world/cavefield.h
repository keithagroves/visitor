#pragma once

#include "planet.h"
#include "nearfield.h"
#include "../graphics/renderer2d.h"

// What grows in the natural caves (Planet::caveProps, placed by
// Planet::buildCaves): stalactites, stalagmites, columns, crystal clusters,
// glowcap mushrooms, gold veins — and the rubble round a cave's mouth. This is
// to a cave what NearField is to the surface: one place that knows where each
// thing IS as drawn, so drawing, cursor picking and the laser agree.
//
// Like everything else on the walking line, a prop is drawn WALK_DROP below
// where it physically is (see the body shader: dug space is drawn with the
// same drop). Props never collide: like the plants, they stand just behind or
// just in front of the plane you walk in.
//
// Art style: two flat tones a formation (a lit and a shaded face), in the
// tone of the cave wall behind it (the ground it is cut in, a shade darker
// with depth — see holeShade in the body shader), and always a step lighter
// than that wall so it reads against it. Crystals and glowcaps are emitted light — full
// color at any hour, each with ONE smooth halo — and they light the cave wall
// behind them (lights(), fed to the body shader).
namespace CaveField {

struct Frame {
    Vec2 focus;                  // camera focus, world space
    float reach = 1000;          // how far from it anything can be on screen
    float time = 0;              // animation clock
    float light[3] = {1, 1, 1};  // day/dusk/night light on the ground here
    float inside = 0;            // 0 = the player is on the surface, 1 = down a hole
    float strength = 1;          // surface-dressing fade (0 = zoomed too far out to draw)
    float holeFloor = 0.34f;     // the body shader's hole shade (gDebug.holeFloor / holeDepth):
    float holeDepth = 150.0f;    //   the darkest a cave wall gets, and from what depth
};

// Where a prop is, in world space, as drawn.
Vec2 root(const Planet& p, const Planet::CaveProp& q);
Vec2 center(const Planet& p, const Planet::CaveProp& q);
float radius(const Planet::CaveProp& q);

// Mining rules. Resources: 1 = ferrite, 2 = the world's crystal (as
// NearField), 3 = calcite, 4 = glowcap, 5 = gold.
NearField::Yield yield(const Planet::CaveProp& q);
float toughness(Planet::CaveProp::Kind kind);     // beam-seconds to mine one
bool alive(const Planet::CaveProp& q);
void color(const Planet& p, Planet::CaveProp::Kind kind, float out[3]);   // its resource color

// Which props a draw call covers. The calcite formations (stalactites,
// stalagmites, columns) grow out of the cave wall: the caller clips them to
// the cave's open space so the rock hides their roots. Everything else —
// crystals, glowcaps, gold set in the wall, rubble at a mouth, every halo —
// is drawn as is.
enum class Part { Loose, Walled };
bool walled(Planet::CaveProp::Kind kind);

// Draw the props behind the walker (front = false) or over them (front =
// true). Inside an active world-space batch, after the body pass and BEFORE
// the front-ground veil, which is what hides a cave's contents from the
// surface. `hot` is the index under the laser (-1 = none).
void draw(Renderer2D& r, const Planet& p, const Frame& f, bool front, int hot, Part part);

// Index of the live prop under `cursor`, within `range` of `from`, that the
// tool at `eye` (a physical point) has a clear line to — or -1.
int pick(const Planet& p, const Frame& f, Vec2 cursor, Vec2 from, Vec2 eye, float range, float slack);

// Per-frame upkeep: glowcaps grow back, a half-mined prop recovers when left
// alone. `cutting` is the index under the laser right now (-1 = none).
void update(Planet& p, float dtSeconds, int cutting);

// Mine a prop out. Formations and ore are gone for good; glowcaps regrow.
void mine(Planet::CaveProp& q);

// The glowing props nearest the camera, as lights for the cave walls.
// Positions are physical (not dropped): the shader works in that space.
struct Light { Vec2 pos; float radius; float r, g, b; };
int lights(const Planet& p, const Frame& f, Light* out, int maxCount);

}  // namespace CaveField
