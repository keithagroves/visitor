---
name: input
description: The Input namespace — bridges Emscripten DOM keyboard, mouse, pointer-lock, and wheel events into a simple pollable state (isKeyDown, mouse/wheel deltas) the game loop reads each frame.
tags: [input, emscripten, events, browser]
---

# Input

A small browser-event bridge. It registers Emscripten DOM callbacks on the
canvas and exposes a pollable snapshot, so the game loop can ask
"is W down this frame?" without dealing with event plumbing.

```mermaid
flowchart TD
    DOM["Browser DOM events<br/>keydown/up · mousemove · wheel"]
    Input["Input namespace<br/>key table · mouse/wheel deltas · pointer lock"]
    Loop["Core loop<br/>pollInput · updateZoom"]

    DOM -->|Emscripten html5 callbacks| Input
    Input -->|isKeyDown / getWheelDelta| Loop

    click DOM call navigate("../../src/input/input.cpp")
    click Input call navigate("../../src/input/input.cpp")
    click Loop call navigate("core.md")
```

## Responsibilities

- **Init** — `Input::init("#canvas")` wires Emscripten `html5` key/mouse/wheel
  callbacks and defines the `Key` enum (WASD, arrows, space, E, etc.).
- **Poll state** — `isKeyDown(code)` for held keys; `getMouseDeltaX/Y` +
  `resetMouseDelta` and `isPointerLocked` for pointer-lock look; `getWheelDelta`
  + `resetWheelDelta` feed the camera's manual zoom. The core loop reads all of
  these each frame in `pollInput`, `pollInputAstronaut`, and `updateZoom`.

## Source

- [src/input/input.h](../../src/input/input.h) ·
  [input.cpp](../../src/input/input.cpp) — the Emscripten input bridge and key
  codes.

## Related

- Who polls it each frame: [core](core.md), [frame loop](../flows/frame-loop.md).
