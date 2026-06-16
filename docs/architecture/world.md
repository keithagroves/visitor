---
name: world
description: The procedural Planet (star-shaped Perlin silhouette, 1/r^2 gravity, atmosphere shell) and its destructible TerrainField — a signed-density grid meshed with marching squares that digging carves into.
tags: [world, planet, terrain, procedural, marching-squares]
---

# World

The planet the whole game orbits. `Planet` owns the procedural silhouette,
gravity, and atmosphere; `TerrainField` is the destructible crust underneath it,
a density grid that digging carves and that re-meshes on demand.

```mermaid
flowchart TD
    Planet["Planet<br/>silhouette · gravity · atmosphere · surface LUT"]
    Field["TerrainField<br/>signed-density grid · marching squares"]
    Noise["PerlinNoise<br/>star-shaped heightfield seed"]
    Renderer["Renderer2D"]

    Planet -->|seeds from| Noise
    Planet -->|owns / seeds / digs| Field
    Field -->|mesh() triangles| Planet
    Planet --> Renderer

    click Planet call navigate("../../src/world/planet.cpp")
    click Field call navigate("../../src/world/terrain_field.cpp")
    click Noise call navigate("../../src/math/noise.h")
    click Renderer call navigate("graphics.md")
```

## Responsibilities

- **Planet** — generates a star-shaped terrain silhouette (radius varies with
  angle via Perlin noise plus a couple of "mountain range" clusters), a 1/r²
  gravity field (`setDensity` sets the GM term), and a soft atmosphere shell
  (`atmosphereOuterRadius()`). Gameplay reads `surfaceRadius(angle)` from a fast
  CPU `surfaceLUT` rebuilt from the destructible field, so collision, landing,
  and the shader silhouette all reflect dug craters. `dig()` forwards into the
  field then rebuilds the LUT. A starter planet is ~32000 units across.
- **TerrainField** — a 2D signed-density grid in world space, organized around
  the planet centre. Each corner stores density (>0 solid rock, <0 open/cave,
  0-isoline = surface), seeded from the planet heightfield. Only the crust shell
  `[coreRadius..outerRadius]` is meshed with marching squares; the interior is a
  cheap always-solid fan. `dig()` pushes corner densities negative inside a
  circular brush and flags dirty; `mesh()` lazily rebuilds the cached world-space
  triangle soup that the core's terrain pass uploads to the GPU.
- **Perlin noise** — supplies the octave noise that shapes the silhouette (from
  [foundation](foundation.md)).

## Source

- [src/world/planet.h](../../src/world/planet.h) ·
  [planet.cpp](../../src/world/planet.cpp) — procedural generation, gravity,
  atmosphere, the surface-radius LUT, and `dig()`.
- [src/world/terrain_field.h](../../src/world/terrain_field.h) ·
  [terrain_field.cpp](../../src/world/terrain_field.cpp) — the destructible
  density grid and its marching-squares mesh.

## Related

- The noise primitive that seeds the silhouette: [foundation](foundation.md).
- Who reads gravity/surface and who digs: [entities](entities.md).
- How the mesh + silhouette LUT reach the GPU each change: [core](core.md).
