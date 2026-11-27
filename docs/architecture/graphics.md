---
name: graphics
description: The immediate-mode Renderer2D — a p5.js-style drawing API (transform stack, fill, primitives, sprites) that batches CPU-transformed geometry into one dynamic VBO — plus the Shader compile/link helper and Plume, the small fluid simulation behind the ship's exhaust.
tags: [graphics, renderer, batching, webgl, shaders, fluid, exhaust]
---

# Graphics

A thin 2D drawing layer that lets the ported game code read like its p5.js
original: set a fill, push/translate/rotate/scale, emit shapes. Geometry is
transformed on the CPU and batched; the trivial GL shader just draws the batch.
(The *fullscreen* post-process shaders — atmosphere, clouds, terminator — are
defined in the [core](core.md), not here; this layer only supplies the
compile helper they use.)

```mermaid
flowchart TD
    Renderer["Renderer2D<br/>transform stack · primitives · batch VBO"]
    Shader["Shader::createProgram<br/>compile + link GLSL"]
    Plume["Plume<br/>exhaust: Stable Fluids on a small grid"]
    Math["vecmath.h<br/>Vec2 · Mat3"]
    GL["WebGL2 / GLES3"]

    Renderer -->|model * view transforms| Math
    Renderer -->|builds program via| Shader
    Renderer --> GL
    Shader --> GL
    Plume -->|positions, velocities| Math

    click Renderer call navigate("../../src/graphics/renderer2d.cpp")
    click Plume call navigate("../../src/graphics/plume.cpp")
    click Shader call navigate("../../src/graphics/shader.cpp")
    click Math call navigate("foundation.md")
    click GL call navigate("../../src/core/main.cpp")
```

## Responsibilities

- **Renderer2D** — immediate-mode batcher. A `Mat3` transform stack
  (`push/pop/translate/rotate/scale`) mirrors p5; primitives (`triangle`, `quad`,
  `circle`, `ellipse`, `line`, `fanPolygon`, `convexPolygon`, per-vertex
  `triangleV`, textured `sprite`) emit CPU-transformed vertices into one
  interleaved dynamic VBO. The batch flushes when the blend mode changes
  (`Normal`/`Additive`) or at frame end. `begin(view,...)` maps world→clip with
  +y down; `beginScreenSpace()` emits directly in clip space for the starfield.
- **Shader** — a tiny namespace: `compile()` a stage and `createProgram()` to
  link a vert+frag pair. Used by the renderer for its batch shader and by the
  core for every fullscreen pass.
- **Plume** — the ship's exhaust as a fluid: an 88 × 88 grid of gas (4.8
  world units a cell) stepped on the CPU with Jos Stam's *Stable Fluids*,
  after the walkthrough at
  [myzopotamia.dev](https://myzopotamia.dev/navier-stokes-fluid-simulation-explained-with-godot):
  each frame the velocity is diffused, projected, advected by itself and
  projected again; then the stuff the cells hold is diffused, advected by
  the flow and faded. "Project" is the pressure solve (Gauss-Seidel) that
  keeps the flow incompressible, which is what makes a jet curl into
  billows and spread along the ground. On top of the walkthrough: two
  kinds of stuff — **fire** (gone in a fraction of a second) and **smoke**
  (what fire cools into where there is air; it lingers and rises); an open
  edge instead of a box; cells in the ground as walls
  (`markGround`); buoyancy, a little drag, vorticity confinement; pressure
  solves that start from last frame's answer; and a grid that follows the
  ship by whole-cell shifts (`follow`). No GL: `texels()` is what the core
  uploads and draws (see *Exhaust* in [core](core.md)). About 1 ms a frame
  in the browser, and only while there is gas in it.

## Source

- [src/graphics/renderer2d.h](../../src/graphics/renderer2d.h) ·
  [renderer2d.cpp](../../src/graphics/renderer2d.cpp) — the immediate-mode
  batching renderer and its drawing primitives.
- [src/graphics/shader.h](../../src/graphics/shader.h) ·
  [shader.cpp](../../src/graphics/shader.cpp) — GLSL compile/link helper.
- [src/graphics/plume.h](../../src/graphics/plume.h) ·
  [plume.cpp](../../src/graphics/plume.cpp) — the exhaust's fluid solver
  (`Plume::Tuning` holds its numbers).

## Related

- The fullscreen shader passes that use `Shader::createProgram`: [core](core.md).
- The `Vec2`/`Mat3` math the transforms are built on: [foundation](foundation.md).
