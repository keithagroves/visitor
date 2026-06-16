---
name: entities
description: The interactive bodies — the player Lander (gravity/thrust/drag physics, tractor beam, trajectory prediction), the abductable Cow (ground→carried→falling→stowed state machine), and the spacewalking Astronaut (walk/jump/jetpack, digs the crust).
tags: [entities, lander, cow, astronaut, physics]
---

# Entities

The three gameplay actors. All live in planet world-space, read gravity and the
surface silhouette from the [Planet](world.md), and draw themselves through the
[Renderer2D](graphics.md). They are forward-simulated by the
[core loop](core.md) each frame.

```mermaid
flowchart TD
    Lander["Lander<br/>player ship"]
    Cow["Cow<br/>abductable, state machine"]
    Astronaut["Astronaut<br/>spacewalk, jetpack, digging"]
    Planet["Planet<br/>gravity · surface · dig()"]
    Renderer["Renderer2D"]

    Lander -->|gravity, contact, beam clip| Planet
    Cow -->|origin, surface| Planet
    Astronaut -->|walk surface, dig| Planet
    Lander --> Renderer
    Cow --> Renderer
    Astronaut --> Renderer
    Lander -. beam catches .-> Cow

    click Lander call navigate("../../src/entities/lander.cpp")
    click Cow call navigate("../../src/entities/cow.cpp")
    click Astronaut call navigate("../../src/entities/astronaut.cpp")
    click Planet call navigate("world.md")
    click Renderer call navigate("graphics.md")
```

## Responsibilities

- **Lander** — the player ship. 1/r² gravity, thrust along the local "up",
  multiplicative atmospheric drag with substepping, and a velocity clamp
  (ported from `lander.js`). Owns the tractor-beam cone (`updateBeam` builds a
  terrain-clipped triangle used for both rendering and catch tests), the
  landing/crash/lift-off transitions, and `predictTrajectory()` for the coast
  path drawn in space. The core loop reads `gravityAt()` for the camera roll.
- **Cow** — runs a small state machine: `OnGround` (wobbles back toward its
  origin) → `Carried` (reeled up the beam, capped speed because it's heavy) →
  `Falling` (released, under gravity) → `Stowed` (reached the hatch; the frame
  it stows, `update()` returns true so the loop bumps cargo + score). The beam
  catch test lives in the core loop (`runBeamGrab`/`pointInTriangle`).
- **Astronaut** — the out-of-ship character: walks along the surface, jumps,
  and flies a fuel-limited jetpack. While spacewalking it can `dig()` the
  crust (the core loop's `doDig()` carves the [TerrainField](world.md) and
  re-uploads the mesh + silhouette LUT). Spawned/recalled by pressing E near
  the landed ship.

## Source

- [src/entities/lander.h](../../src/entities/lander.h) ·
  [lander.cpp](../../src/entities/lander.cpp) — ship physics, tractor beam,
  landing state transitions, trajectory prediction.
- [src/entities/cow.h](../../src/entities/cow.h) ·
  [cow.cpp](../../src/entities/cow.cpp) — abductable cow state machine and draw.
- [src/entities/astronaut.h](../../src/entities/astronaut.h) ·
  [astronaut.cpp](../../src/entities/astronaut.cpp) — spacewalk movement,
  jetpack, and crust digging.

## Related

- Gravity, surface radius, and the destructible crust they act on:
  [world](world.md).
- How they are updated and drawn each tick: [frame loop](../flows/frame-loop.md).
