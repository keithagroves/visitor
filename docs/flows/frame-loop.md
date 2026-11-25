---
name: frame-loop
description: What mainLoop() does every tick — sync tunables, edge-trigger E/R/M/thrust keys, advance the orbits, run the state machine (input → physics → contact → beam → cows), move the astronaut, update camera, then the layered render (stars → sky band → sky bodies → ridges → water → body pass → shadows/cows → face fill → nav overlay → ship → water in front → foreground → clouds → vignette) and the HUD push.
tags: [flow, game-loop, render-order, update]
---

# The frame loop

`mainLoop()` runs once per animation frame (driven by
`emscripten_set_main_loop`). It splits into an **update** half (advance the
simulation by a frame-relative `timeScale`) and a **render** half (draw the
scene in strict back-to-front layers so the sky correctly occludes stars and the
night side darkens before clouds go on top). Source:
[main.cpp](../../src/core/main.cpp).

## Update phase

```mermaid
sequenceDiagram
    participant Loop as mainLoop
    participant JS as window.DBG (shell.html)
    participant In as Input
    participant L as Lander
    participant P as Planet
    participant Cows as Cow herd
    participant A as Astronaut
    participant V as View

    Loop->>JS: syncDebugFromJS() — pull live tunables into gDebug
    Loop->>In: read edge keys (E enter/exit, R restart, M map, thrust liftoff)
    Note over Loop: the title screen freezes everything (dt = 0) until gameStart
    Loop->>P: updateOrbit() for every body (parents first), re-pick dominant body — repeated 4 or 16 times a frame under time warp (T), with the ship's physics
    Loop->>Cows: carry cows by the home planet's orbitStep
    alt state == PLAYING
        Loop->>In: pollInput()
        Loop->>L: update(planet, timeScale) — gravity, thrust, drag
        Loop->>P: updateContact() — land? crash?
        Loop->>L: updateBeam(planet) — build tractor cone
        Loop->>Cows: runBeamGrab() + cow.update() → cargo/score
    else state == LANDED
        Loop->>L: stick to body, share its velocity, refuel; thrust key → liftOff()
        Loop->>Cows: cow.update() — a cow over a fresh hole falls in
    else state == CRASHED
        Loop->>L: wreck stays on the body; R key → resetGame()
    end
    opt spacewalking
        Loop->>A: carry by the body's orbitStep
        Loop->>P: focusCarve() — keep the fine dig layer around the player
        Loop->>In: pollInputAstronaut() (walk/jump/jetpack, swim up / dive in water), Q switches tool
        Loop->>A: update(planet, timeScale)
    end
    Loop->>P: Outposts::update() — the locals wander, look, wave; which trader is in reach (E trades)
    Loop->>A: updateSuit() — the suit meter drains or refills; F breathes carbon, C scans, J jumps, the herd regrows
    Loop->>P: updateDiscovery() — chart worlds on approach
    Loop->>V: updateZoom() + updateView() — framing, follow, auto-roll, map
    Loop->>A: updateSword() — sword out: a click is a cut toward the cursor; what it hits, hit-stop, pogo (after the camera)
    Loop->>A: updateLaser() — aim at the cursor: harvest, or cut terrain (held off mid-swing)
    Loop->>P: updateEnemies() — crawlers after dark, bats in the caves: move, pounce / dive; a bite comes off the suit
    Loop->>P: NearField::update() — regrowth; updateWaterFx() — splashes, spray, bubbles, the floating ship; updateParticles()
```

## Render phase

Drawn in this order so each layer composites correctly over the last:

```mermaid
flowchart LR
    Clear["glClear + view matrix"] --> Stars["1. Starfield + star<br/>(screen-space / world)"]
    Stars --> Occ["1b. Star occluders<br/>ridge + sky-body shapes in black"]
    Occ --> Sky["1c. Sky band<br/>atmosphere mode 0 + horizon haze"]
    Sky --> Ridges["1d. Ridge layers<br/>nearest first, stamp stencil"]
    Ridges --> Dome["1d1. Behind the ranges (stencil-clipped)<br/>sky dome · far clouds · shooting star"]
    Dome --> Shafts["1e. Light shafts"]
    Shafts --> Water["1f. Water, behind the crust<br/>(water pass, mode 0)"]
    Water --> Terrain["2. Planet body pass<br/>SDF fill · material · slope light · terminator"]
    Terrain --> Mask["2'. Cave mask<br/>body pass, mask mode: stencil bit over open cave space"]
    Mask --> Cows["2b. Cast + blob shadows, cows,<br/>harvestables behind the walking line,<br/>trading posts and the locals,<br/>uncovered ore, cave props"]
    Cows --> Atmo["3. Face fill<br/>atmosphere mode 1 (from space)"]
    Atmo --> Nav["3b. Nav overlay<br/>orbit rings · markers · labels"]
    Nav --> Ship["3c. Ship, crawlers, bats, astronaut<br/>+ trajectory/beacon aids"]
    Ship --> WaterF["3c-. Water in front of the walker<br/>+ in flooded holes (water pass, mode 1)"]
    WaterF --> Veil["3c0. Front-ground veil<br/>over dug holes (body pass, veil mode)"]
    Veil --> WaterB["3c0'. Water over the bed in front<br/>(water pass, mode 2)"]
    WaterB --> Near["3c1. Harvestables in front<br/>+ mining laser, sparks, chips, spray"]
    Near --> Fg["3d. Foreground band<br/>(screen-anchored)<br/>+ scanner marks over it"]
    Fg --> Clouds["4. Clouds<br/>FBM band"]
    Clouds --> Top["5. Motes + vignette<br/>+ red edges after a bite"]
    Top --> HUD["updateHUD() / updateNav() → DOM overlay"]

    click Stars call navigate("../../src/core/main.cpp")
    click Ridges call navigate("../../src/core/main.cpp")
    click Terrain call navigate("../../src/core/main.cpp")
    click Cows call navigate("../../src/entities/cow.cpp")
    click Atmo call navigate("../../src/core/main.cpp")
    click Ship call navigate("../../src/entities/lander.cpp")
    click Clouds call navigate("../../src/core/main.cpp")
    click HUD call navigate("../../shell.html")
```

Key ordering reasons (from the comments in `mainLoop`):

- **Ridge silhouettes** draw before the body pass, so only peaks poking above
  the main silhouette stay visible as distant horizon mountains.
- The ridge layers are translucent so the **sky** tints them into haze, but
  nothing else may show through a mountain. Three steps keep it that way:
  the **star occluders** (ridge and sky-body shapes in opaque black, drawn
  right after the starfield and before the sky — invisible against space,
  but they erase the stars behind); the ridge color pass, drawn **nearest
  layer first with a stencil test** so each pixel is owned by the nearest
  range and blends with the sky only; and `beginBehindRidges()`, which
  clips everything that lives behind the ranges — the **sky dome** (the star
  and the other worlds at their true directions), the far cloud band, the
  shooting star — to pixels no ridge covers. So bodies set cleanly behind
  the mountains and then the terrain. The WebGL context is created with a
  stencil buffer for this.
- The **body pass** (one fullscreen draw) fills the rock from the destructible
  density-field texture, applies the altitude-banded material, slope lighting
  from the SDF gradient, the day/night terminator, and the cave-interior
  backdrop (a darker shade of the ground, deeper is darker). It runs *before* the cows so abductable cows stay readable at night.
- Right after it, a third, color-less run of the body pass (`drawCaveMask`)
  marks the cave's open space, as drawn, in stencil bit `0x80` (above the
  ridge counts). The calcite formations (`CaveField::Part::Walled`) are drawn
  inside `beginInCave`/`endInCave`, clipped to it, so the wall they grow out of
  hides their flat bases. The frame's stencil clear wipes the bit.
- **Water** goes down *before* the body pass, as a disc at each wet world's
  sea radius: the crust is painted over it, so it shows only where the
  ground is lower than the sea, and the body pass's own anti-aliased ground
  line is its bed. Two more runs put it in front of things: after the ship
  and the astronaut (tinting whatever is in the water, and filling flooded
  holes) — *before* the front-ground veil, which is ground in front of a
  hole's water and must cover it from the surface — and after the veil,
  over the strip of lake bed in front of the walking line.
- **Ground blob shadows** draw between the body and the entities, grounding the
  ship/astronaut/cows on the surface (or on the water, over water).
- The **atmosphere** is one disk drawn after the planet body; the shader splits
  it by radius (horizon sky above the mountains, altitude-gated sky sphere over
  the planet face), so the ground stays clear up close.
- **Harvestables** are split around the walker: the ones behind the walking
  line draw before the ship and astronaut, the ones in front draw after — you
  walk behind the grass. The **mining laser** and its particles go over both.
  Ore the cutter has uncovered sits in the rock: it draws with the things
  behind the walker, before the cave props.
- **Trading posts** (hut, stall, trader, the locals out walking) draw with
  the things behind the walking line, so the ship and the astronaut pass in
  front of them; the lantern's and the window's halos are one additive
  group at the end of each post.
- The **foreground band** draws after the ship because it is the nearest depth
  plane; it is anchored to the bottom of the screen, so it frames the scene
  without ever covering the player.
- **Clouds** draw after that (in front of the ship) so you fly behind them. In
  space, a locator **beacon** + heading arrow overlay the sub-pixel ship.
- Every screen-space pass wraps itself in `begin()`/`end()`: a batch left
  pending is discarded by the next `begin()`.

## Files in this flow

- [src/core/main.cpp](../../src/core/main.cpp) — `mainLoop`, the state machine,
  the shader passes, and the draw ordering.
- [src/entities/lander.cpp](../../src/entities/lander.cpp),
  [cow.cpp](../../src/entities/cow.cpp),
  [astronaut.cpp](../../src/entities/astronaut.cpp) — the actors updated/drawn.
- [src/world/planet.cpp](../../src/world/planet.cpp) — gravity, surface, orbit, dig.
- [src/world/system_gen.cpp](../../src/world/system_gen.cpp) — the system the
  loop advances (built once in `init()`).
- [src/input/input.cpp](../../src/input/input.cpp) — the polled key/wheel state.
- [shell.html](../../shell.html) — the HUD overlay updated each frame.

## Related

- Subsystem map: [overview](../overview.md).
- The orchestrator that owns this loop: [core](../architecture/core.md).
