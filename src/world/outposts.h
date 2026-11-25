#pragma once

#include <string>
#include "planet.h"
#include "nearfield.h"
#include "../graphics/renderer2d.h"

// The locals and their trading posts (Planet::folk, Planet::outposts — placed
// by Planet::foundOutposts).
//
// A post is a little group of things standing on the strip of ground just
// behind the walking line, so whoever walks past is in front of it: a domed
// hut, a market stall with the trader behind its counter, a mast with a
// lantern (the thing you spot from the air, and at night), and a few crates.
// One or two more of the locals wander the ground on either side. Walk up to
// the stall and the trader will buy what you are carrying and sell you
// upgrades (the rules for that are in core/trade.h; this module only knows
// where everything is and how it looks).
//
// Like the plants (NearField) the pieces are rooted: each stands where the
// ground is at its own angle, there is no parallax, and everything is drawn
// in the hour's light. Art style (docs/art-style.md): flat tones — a lit and
// a shaded one per shape plus one accent, the world's own crystal color,
// which is what its people wear and paint their stalls with. The lantern and
// the hut's window are emitted light: one smooth halo each, on at dusk.
namespace Outposts {

// How close to the trader (world units) you have to be to trade.
constexpr float TRADE_REACH = 85.0f;
// The cutter leaves the ground around a post alone (the post stands on the
// terrain's outline, like the ship, and would be left hanging over a hole).
constexpr float NO_DIG = 215.0f;
// The wanderers keep within this far of the stall, along the ground.
constexpr float WANDER = 170.0f;

// The point on the ground at the stall (physical: on the terrain line).
Vec2 ground(const Planet& p, const Planet::Outpost& o);
// Where the trader's head is on screen (world space, as drawn): for a marker
// over it.
Vec2 traderHead(const Planet& p, const Planet::Outpost& o);

// The post nearest a world point, measured along the ground: its index (or
// -1 on a world with none) and, in `along`, how far it is — positive when it
// lies the way angles grow (screen right, with the camera rolled upright).
int nearest(const Planet& p, Vec2 world, float* along);
// The post whose trader `world` is within TRADE_REACH of, or -1.
int inReach(const Planet& p, Vec2 world);
// Would a cutter bite (a circle, world space) cut into a post's ground?
bool blocksDig(const Planet& p, Vec2 world, float radius);

// A post closes for the night: when the sun at it is this far below the
// horizon (the dot of "up" there with the way to the sun), and opens again
// when it comes back up past it.
constexpr float CLOSE_SUN = -0.06f;

// Per-frame life: the wanderers walk and stop, everyone turns to look at a
// visitor who comes close, the trader waves once when they arrive. `visitor`
// is the player on foot (`present` false = nobody to look at). `toSun` (unit,
// from the planet's center) opens and shuts the posts with the day: at night
// the trader and the others are indoors and the stall is shuttered.
void update(Planet& p, float dt, Vec2 visitor, bool present, Vec2 toSun);

// Draw every post in view. Inside an active world-space batch, after the
// body pass and before the ship and the astronaut (they pass in front).
// `pxPerUnit` is the view scale: the lantern's halo is kept a few pixels
// across however far out the camera is, so a post can be spotted from the
// air. `active` is the post whose trader can be traded with right now (-1 =
// none): it gets a little speech bubble.
void draw(Renderer2D& r, const Planet& p, const NearField::Frame& f, float pxPerUnit, int active);

// A few words in the trader's own tongue (the same every visit).
std::string phrase(const Planet::Outpost& o);

}  // namespace Outposts
