---
name: art-style
description: The game's visual language — limited palette, flat banded color fields, cel-step lighting, soft computed skies, and the ground-ellipse rule. The reference doc every new visual feature must follow.
tags: [art, style, palette, reference]
---

# Art style — minimal color

References: **A Short Hike** (flat limited-palette color fields, banded light)
and **Summer Afternoon** by vlucendo (soft pastel skies, layered hazy hills).
The core contrast of this game's look: **flat, banded land under a soft,
computed sky.**

## Rules

1. **Terrain is five flat tones**, picked by altitude with noise-jittered
   boundaries (ragged shorelines, never concentric rings). No per-pixel grain,
   no texture noise inside a color field.

   | tone | color | hex |
   |---|---|---|
   | basin | dark warm grey | `#4a4540` |
   | mid | rock grey | `#6e6964` |
   | high | pale khaki | `#8f887e` |
   | crest | light bone | `#b0a89a` |
   | snow | cool white | `#edf2fa` |

2. **Lighting is three cel steps** — night ×0.30, shadow ×0.62, day ×1.0 —
   with thin anti-aliased edges. The day/night terminator reads as a clean
   swept band; shadow-side mountain flanks drop exactly one step.

3. **The sky stays soft.** Atmosphere (Rayleigh/Mie), clouds, and the
   terminator glow are smooth and computed; they are the painterly half of the
   look. Don't posterize them.

4. **Background ridges are one flat color per layer** (olive → sage → lavender,
   hazier with distance), darkened across the terminator and hue-shifted to
   ember inside the dusk band. No gradients inside a layer, no rim light.
   Layers are occluded by the terrain via draw order, never clipped against it.

5. **Ground-plane circles** (blob shadows, beam splats, future pads / craters /
   puddles / buried bases) are ellipses with height **~64% of width** — a ~40°
   view angle above the ground plane (sin 40°), shared by everything that lies
   flat. The contact point sits **one ellipse half-height below the terrain
   silhouette line**, and everything standing on the ground (ship, astronaut,
   cows) rests its base on that point — the strip of ground in front of each
   entity is what sells the angled, 3D look.

6. **Depth comes from stacked parallax planes, not a second camera.** Far→near:
   looming sky body → far cloud band → six haze ridge layers → light shafts →
   planet body (+ long cast shadows) → ground tufts → two foreground framing
   planes (+ swaying props, drawn OVER the player) → dust motes → vignette. Each
   plane is hazier/slower with distance (`follow` rises with depth); the nearest
   foreground is darkest and slides fastest. Gradients are allowed only in the
   atmospheric layers (shafts, clouds, motes, vignette) — terrain and ridge
   *shapes* stay flat per rule 1/4.

7. **Don't:** fine noise grain, hatching/stippling, gradients inside terrain or
   ridge shapes, more than ~6 simultaneous hues in a scene.

## Where it lives

- Terrain tones + cel steps: `kBodyFrag` in [main.cpp](../src/core/main.cpp).
- Ridge palette: the `specs[]` table in [planet.cpp](../src/world/planet.cpp)
  (tint/alpha/skyMix per layer) + `drawRidges` in main.cpp (dusk/night grading).
- Ground ellipses: `drawBlobShadow` (main.cpp), beam splat (lander.cpp).
- Depth stack (main.cpp): `drawLoomingBody`, `drawFarClouds`, `drawRidges`
  (background + foreground passes), `drawLightShafts`, `drawTerrainShadows`,
  `drawProps` (tufts + foreground props, data in `Planet::props`), `drawMotes`,
  `drawVignette`. Ridge + prop layer params: the `specs[]` table in planet.cpp.
- Planet archetypes (Stage 4) swap the five terrain tones + ridge tints per
  world; the rules above stay fixed.
