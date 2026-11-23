# Plan: From sandbox to exploration game

Vision: a No Man's Sky / KSP-flavored 2D game — fly between planets in a small
solar system, discover unnamed worlds, land on them (landing itself is the
challenge), step out and dig. Planets must *look* like places: lit slopes, ground
shadows, horizon depth, other worlds visible in the sky — not flat grey discs you
perch on the rim of.

Decisions already made:
- **Keep the destructible density field** ([src/world/terrain_field.cpp](src/world/terrain_field.cpp))
  as the source of truth for collision and the silhouette LUT, but **render it as
  an SDF mask texture** sampled in a fullscreen "body" shader pass instead of a
  marching-squares mesh. Digging = carve the CPU grid + `glTexSubImage2D` of the
  dirty rect (no mesh rebuild). The field also goes **lazy**: only the planet
  you're near owns a live grid; distant planets are analytic silhouettes. Digs
  are recorded and replayed so they survive field unload/reload.
- **No parallax starfield.** Depth comes from the planets themselves.
- The p5 original (the `cow-abductor` p5.js sketch) already has
  tuned implementations of orbits, discovery, map mode, ridges, water, and biome
  texture — port, don't reinvent. Math carries over (degrees, +y down).

Each stage leaves the game playable.

**What the game IS** — goal, loop, the equipment ladder, what failing costs —
is decided in [docs/gameplay.md](docs/gameplay.md) (2026-11-22), and all of
its build order was built on 2026-11-23: saving, the equipment ladder, pack
and hold, the suit meter, the tool walls, the fuel wall and time warp, the
star drive and the jump, and a title screen. It replaces Stage 5 below; this
file remains the record of how the world got built.

**Status (2026-11-15).** Stages 1–2 are done and Stage 3 mostly so; Stage 4's
archetypes arrived early as part of system generation.
- The system is now **generated from a seed** ([system_gen.cpp](src/world/system_gen.cpp)):
  star + 4–6 planets + moons, each with an archetype look, on Kepler-consistent
  orbits around a real central star (the "sun orbits home" stand-in is gone).
- Discovery, first-landing score, the world/system HUD and the map (M) are in.
- Flight was retuned so the system is reachable: fuel burn 0.2 → 0.05 per
  frame and the tank refills while landed (at 0.2 a full tank ran dry at
  ~8,400 altitude, below the 12,800 atmosphere top).
- Water is in (2026-11-22): one sea level per wet world, so the low ground
  fills into ponds, lakes and seas; the ship floats, the astronaut swims,
  dug space floods where it connects to water. See docs/architecture/world.md.
- The locals are in (2026-11-22): every world has its own kind of people and
  a few trading posts. Sell what you mine (and the cows in the hold) for
  credits, buy upgrades — fuel tank, jetpack, laser, cutter, cargo bay — and
  goods that are worth more on other worlds. This is Stage 5's "upgrades",
  bought with credits rather than score; see docs/architecture/core.md
  (Trading) and world.md (the locals).
- Still open from the stages below: seeded caves, more
  abductables (cows only spawn on the home world), dig feel, the whole of
  Stage 5. New candidates: time warp for long coasts, a pointer to the
  nearest uncharted world, things to do on the other worlds.

---

## Stage 1 — Make the planet read as 3D (visual depth)

The current planet is one flat `u_color` mesh fill plus a terminator multiply
pass. Replace both with a single **body pass** and add depth cues around it:

1. **Body pass (SDF mask texture).** Upload the TerrainField corner grid as an
   R16F texture (filterable in ES 3.0; upload floats directly). A fullscreen
   pass — same world-reconstruction skeleton as the atmosphere/cloud/terminator
   shaders — samples the density per pixel:
   - density ≥ 0 → rock; edge anti-aliased with `fwidth` smoothing at the 0
     iso-line (smoother than the 250-unit marching-squares facets).
   - density < 0 but **below the silhouette LUT** → cave interior: near-black
     rock backdrop, darkening with depth, so tunnels never show stars.
   - density < 0 above the LUT → sky (alpha 0).
   - **Terminator merged in**: same dayNight shade the separate pass computed —
     the terminator pass and the mesh path (`buildMesh`, terrain VAO/VBO,
     `uploadTerrainMesh`) are deleted.
   - Digging = `glTexSubImage2D` of the brush's dirty rect (uses
     `GL_UNPACK_ROW_LENGTH` to upload the sub-rect straight from the CPU grid).

2. **Terrain material, per pixel in the body pass.** Altitude fraction =
   (dist − base) / noise band → color ramp: dark basins → rock → ragged snow
   caps, with FBM grain (reuse the cloud shader's `hash/vnoise/fbm`) jittering
   band edges so nothing reads as concentric rings. Live-shader port of the p5
   baked texture ([planet.js generateSurfaceTexture](../games/cow-abductor/src/world/planet.js#L111)).
   Grass/water bands arrive with archetypes in Stage 4.

3. **Slope lighting from the SDF gradient.** The world-space density gradient
   (4 taps at ±half-cell) is the true 2D surface normal — works on mountainsides
   *and* inside dug craters/tunnels, which the old silhouette-LUT idea couldn't
   see. Brighten sun-facing slopes, darken back slopes; subtle, the terminator
   still owns day/night.

4. **Ground blob shadows.** For lander / astronaut / cows: project the entity
   radially onto `planet.surfaceRadius(angle)`, draw a soft dark ellipse on the
   ground, offset along the surface tangent away from the sun
   (`sunWorldPos()`), alpha and size fading with altitude (gone above a few
   hundred units). Drawn after the body pass, before cows. This single change
   kills the "sticker on a rim" look when landing.

5. **Background ridge silhouettes.** Port `generateBackgroundRidge`
   ([planet.js:88](../games/cow-abductor/src/world/planet.js#L88)): two
   haze-tinted ridge layers per atmosphere planet, drawn with the 2D renderer
   *before* the body pass so only their peaks show above the main silhouette
   as distant horizon mountains. Each layer follows a fraction (0.68 / 0.84) of
   the player's surface angle, so the horizon slides believably as you fly.
   Per-vertex night darkening (gradient triangles) keeps them consistent with
   the terminator.

## Stage 2 — Solar system & travel (background planets for free)

1. **Planet orbits.** Port orbital state from
   [planet.js:208-294](../games/cow-abductor/src/world/planet.js#L208):
   `orbitCenter / orbitRadius / orbitSpeed / orbitAngle`, kinematic update, plus
   the optional symplectic gravity-driven orbit (`useGravityOrbit`) and
   `getOrbitalVelocity()` (needed so landed ships ride the planet — the
   `landingOffset` hook already exists in the LANDED case,
   [main.cpp:1130](src/core/main.cpp#L1130)).
2. **`std::vector<Planet>` + a real sun.** Sun = `isSun` planet at the system
   center; `sunWorldPos()` returns its actual position (day/night now comes from
   orbiting, not the fake drift). Start with: sun, home planet (current one),
   and a **moon orbiting the home planet** — a big object hanging in the sky is
   the fastest "background planets" payoff.
3. **Multi-body gravity.** Sum `gravityAt` over all bodies (lander.h is written
   for this). Dominant body (strongest pull) drives camera roll, altitude
   framing, contact tests, and cow updates.
4. **Shaders go multi-planet.** All four passes already take `MAX_PLANETS=10`
   arrays but are fed only index 0. Fill them per planet. Silhouette LUT becomes
   a 2048×N texture (one row per planet); shaders index row `i`.
5. **Lazy terrain fields.** Split `Planet::generate()`
   ([planet.cpp:28](src/world/planet.cpp#L28)): always build the analytic
   landscape + seed LUT (cheap); build `field` only via `ensureField()` when the
   player is within ~2× atmosphere radius, release when far. Keep a per-planet
   list of `(pos, radius)` dig operations; replay them on rebuild so craters
   persist. Far planets render in the same body pass from their silhouette LUT
   row (radial test, like the old terminator did) — no field texture needed.
6. **Per-planet entities.** Cows spawn per planet; `spawnCows` takes a planet.

## Stage 3 — Discovery & map (the NMS/KSP feel)

1. **Discovery.** `name` + `discovered` on Planet; reveal when the ship comes
   within a discovery distance (p5 used 2500 world units); HUD toast
   "Discovered: <name>" plus score bonus. First *landing* on a body is a bigger
   bonus — landing is the KSP challenge being rewarded.
2. **Overview / map mode (M).** Camera mode that frames the whole system
   (port from sketch.js): orbits as faint circles, planets as discs,
   undiscovered ones drawn dim with "?" labels. The View struct already supports
   everything needed (focus/scale override).
3. **Objective HUD.** Extend the DOM HUD in [shell.html](shell.html): nearest
   body + distance, discovered count, event toasts. Reuses the existing
   `updateHUD` push ([main.cpp:1209](src/core/main.cpp#L1209)).

## Stage 4 — Planet variety & things to find

1. **Archetypes** drive the Stage-1 shader params + atmosphere per planet:
   - airless moon (no atmosphere entry, grey, harsh terminator)
   - lush world (water + grass ramp, thick atmosphere)
   - ice / volcanic variants (color ramps, atmosphere tint)
2. **Water worlds.** Port sea level ([planet.js:475](../games/cow-abductor/src/world/planet.js#L475)):
   water disk behind the crust, shoreline rim, terrain ramp switches to the
   vegetation band. (Underwater physics can come later.)
3. **Seeded caves.** Subtract a noise term from the field seed in
   `TerrainField::generate` so some planets have natural cavern pockets — hidden
   until the astronaut digs into them (the silhouette LUT only sees the
   outermost rock, so they're genuine surprises). The body pass's cave-interior
   shading renders them correctly for free.
4. **More abductables** as exploration rewards: minerals on airless moons,
   plants on lush worlds (port from p5 `mineral.js` / `plant.js`). Cows stay
   the home-planet specialty.
5. **Digging feel.** Particles on dig, brief screen-space dust, hold-to-dig
   cadence — the astronaut implementation stays as-is otherwise.

## Stage 5 — Close the game loop

1. `GameState` gains `WAITING / WON / LOST` (start screen, stranded-out-of-fuel
   lose, goal-met win).
2. Home base on the starter planet: deliver cargo → score + refuel.
3. Goal: discover + land on every body / deliver a quota — pick once Stages 2–3
   are playable and we feel the pacing.
4. Upgrades (fuel tank, beam, cargo) spending score — these are already live
   tunables in `gDebug`, just driven by gameplay instead of sliders.

---

## Order & why

Stage 1 first: it pays off immediately on the current single planet and every
later planet inherits it. Stage 2 is the structural change (vector of planets +
lazy fields) — biggest refactor, do it while the codebase is still small.
Stages 3–5 are additive.

Out of scope for now: parallax starfield (explicitly dropped), true cave
lighting, fluid plumes, audio, hull heat / re-entry.
