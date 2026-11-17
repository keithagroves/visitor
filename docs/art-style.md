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
   no texture noise inside a color field. Band edges are anti-aliased against
   the pixel footprint, so they stay crisp up close. Each world's archetype
   supplies its own five tones (`PlanetLook::tones`); the home world's are:

   | tone | color | hex |
   |---|---|---|
   | basin | dark warm grey | `#4a4540` |
   | mid | rock grey | `#6e6964` |
   | high | pale khaki | `#8f887e` |
   | crest | light bone | `#b0a89a` |
   | snow | cool white | `#edf2fa` |

2. **Day turns to night gradually, in color.** The tones are flat; the light
   across them is a smooth function of the sun's height: white through the
   day, gold and then orange-red as the sun reaches the horizon (the same
   extinction that colors the sky), a rose twilight ambient once it is under,
   and a dim blue night. The terminator is a long graded sweep — golden hour,
   dusk, blue hour — never a line. Clouds run slightly ahead of the ground, so
   they are the last things lit at dusk. Slopes turned well away from the sun
   drop one soft-edged step while there is direct sun. Airless worlds keep a
   hard terminator: white light, no afterglow. One model, shared by every
   pass: `LIGHT_GLSL` / `groundLight()` in main.cpp.

3. **The sky stays soft.** Atmosphere (Rayleigh/Mie), clouds, and the
   terminator glow are smooth and computed; they are the painterly half of the
   look. Don't posterize them.

3a. **The horizon glows.** The lowest slice of air carries a pale aerial haze
   (per-world color, `PlanetLook::haze`) so the sky lightens toward the land
   and the far ranges sit in light. It takes the sunlight's extinction, so it
   warms at dusk. The band is fitted to the view on the world you're at
   (about a third of the visible height, sitting on the ground under the
   player), so it stays a glow along the horizon at a close zoom instead of
   washing the whole sky. From altitude, the top of the atmosphere fades out
   into space — the limb is a soft glow, never a hard-edged disc.

4. **Background ridges are one flat color per layer** (per-world tints lerped
   far → near, hazier with distance), darkened across the terminator and
   hue-shifted to ember inside the dusk band. No gradients inside a layer, no
   rim light. Layers are occluded by the terrain via draw order, never clipped
   against it.

   **Hazy, not see-through.** A range is drawn translucent only so the *sky*
   tints it — that is the distance effect. A mountain is still solid: no star,
   planet, sun, cloud band or farther range may show through it. Each pixel
   belongs to the nearest range covering it and blends with the sky alone.

5. **Ground-plane circles** (blob shadows, beam splats, future pads / craters /
   puddles / buried bases) are ellipses with height **~64% of width** — a ~40°
   view angle above the ground plane (sin 40°), shared by everything that lies
   flat. The contact point sits **one ellipse half-height below the terrain
   silhouette line**, and everything standing on the ground (ship, astronaut,
   cows) rests its base on that point — the strip of ground in front of each
   entity is what sells the angled, 3D look.

6. **Depth comes from stacked parallax planes, not a second camera.** Far→near:
   sky-dome bodies → far cloud band → six haze ridge layers → light shafts →
   planet body (+ long cast shadows) → harvestables behind the walking line →
   the walker → harvestables in front → two foreground framing planes (+
   swaying props) → dust motes → vignette. Each plane is
   hazier/slower with distance (`follow` rises with depth); the nearest
   foreground is darkest and slides fastest. Gradients are allowed only in the
   atmospheric layers (shafts, clouds, motes, vignette) — terrain and ridge
   *shapes* stay flat per rule 1/4.

   - **The ground has depth.** The terrain silhouette is the far edge of the
     visible ground and the walking line sits below it (rule 5's ~40° view).
     Plants and minerals stand in front of or behind that line: further down
     the screen, bigger and darker the nearer they are; hazier toward the
     edge. Flat tones only — two per plant or crystal (a lit and a shaded
     face), in the world's own `flora` / `mineral` colors.
   - **Ground cover is grown, not scattered.** Plants and stones come in
     clumps with a reason to be where they are — meadows on damp level
     ground with flowers in them, scree at the foot of a slope, crystals
     growing out of a rock pile — with bare ground and the odd loner between.
     A clump is tallest in the middle and is a patch in depth, not a row.
     Even spacing reads as things dropped at random.
   - **Things on the ground stay where they stand.** No parallax for
     anything rooted to the ground: there is one ground profile on screen, so
     a plant that slides past faster than it slides ALONG it — and on a slope
     that is up or down the hill, like an escalator. Parallax belongs to
     whole planes that are not standing on that ground (ridges, the
     foreground framing).
   - **A hole opens at the walking line, not at the terrain line.** Whatever
     stands on the walking line is drawn 13 units below the terrain line
     (boots, hooves, blob shadows), so dug space is drawn that much lower
     too: a strip of ground is still visible behind a hole, and the floor of
     a hole is where boots standing on it are drawn. If a hole were drawn
     where it physically is, anyone walking up to it would appear to float
     over it.
   - **Its mouth is an ellipse lying on the ground**, the way a round hole
     looks from a low angle: the far rim arches up behind the middle of the
     hole, the near rim sags below the walking line, and the two meet at the
     hole's sides in a curve (a quarter circle in from each wall) — never a
     straight line across, never a corner.
   - **A hole goes down BEHIND the front ground.** The ground in front of
     the walking line (where the nearer plants stand) starts at the mouth's
     near rim and is drawn back over the hole in the ground's own color.
     *From the surface it is opaque*: a hole is its mouth and nothing else,
     the shaft under it is out of sight. *Down a hole you get the cutaway*:
     the front strip turns see-through around you (`frontVeil` knob) and
     below the strip the rock is simply open, so shafts and tunnels show in
     cross-section. The change eases over a moment as you drop in or climb
     out. Plants on that strip keep their ground; only what stood inside the
     hole's own footprint is uprooted.
   - **Cutting burns.** Rock beside a cut keeps a dark scorched rim (hidden,
     like the walls it is on, when seen from the surface). Where the cutter
     has just been, the rim glows — orange through it, a thin yellow line on
     the cut face — and cools in under two seconds; the glow is emitted
     light, so it shows at night and tints the hole. Each bite throws a few
     embers that fall and, now and then, a small puff of thin smoke. Keep it
     a rim: a glow that fills the hole or smoke that hides the astronaut is
     too much.
   - **A hole is a darker shade of the ground it was cut in — never
     black.** Its color is the ground's own color at that spot (band and
     hour's light, so a hole in snow is darker snow) times a depth shade:
     ~0.88 for a scratch, easing down (most of the fall in the first stretch,
     so the first bites show) to `holeFloor` (0.34) at `holeDepth` (150
     units, about four body heights) and flat below that. A one-foot hole is
     barely darker than the ground; a deep shaft or a cave levels off well
     short of black. In the cutaway the depth is how far down the point you
     see is, so a shaft is lighter at the top and darker down it.
   - **A mouth shows its far wall.** Looking down into a hole from the
     surface you see the wall on its far side, from the lip down: one smooth
     fall from light at the far rim to the shade of the hole's real depth at
     the near rim. So a dent reads as a dent and a deep shaft still reads
     deep, though you only see its mouth. A natural cave's mouth counts as
     deep. A flat dark shape laid on the ground reads as a sticker, not a
     hole. Applies to every hole, dug or natural.
   - **Natural mouths are uneven; dug ones are clean.** A cave's rims wander
     in lumps a few strides long (shape, never grain) and rubble lies round
     the opening; a cut hole keeps its clean ellipse and its scorch.
   - **Caves are dim, and lit by what is in them.** The back wall is the
     hole rule at full depth: the ground's tone at `holeFloor`. Formations
     take the tone of the wall behind them, in two flat tones (a shaded and
     a lit face) a clear step lighter than it, so they read against it; at
     night their light is lifted like the plants'. Crystals and
     glowcaps are emitted light: full color at any hour, each with one smooth
     halo, and they light the wall behind them in their own color — pools of
     light, soft-capped so a room full of crystals never goes white. The
     player carries a faint lamp underground. Keep it dim: the astronaut and
     the things to mine must stay the brightest things in a cave.
   - **Dust motes are off by default** (`motes` knob). As small pale squares
     drawn over the whole frame they read as stars showing through the
     mountains and the ground. If they come back they need to read as dust,
     not as points of light.
   - **The foreground planes are anchored to the bottom of the screen**, not to
     the planet: they frame the scene and can never rise over the player, so
     nothing is cut out of them. They still read as near geometry — a fixed
     profile around the planet scrolled faster than the terrain, dropping away
     quickly on lift-off, shrinking off-frame when the camera pulls back.
   - **The horizon sits on the lower third** when grounded (the camera looks a
     little above the player), so the sky gets the frame and the rock
     cross-section below the surface gets less.
   - **Sky bodies are real.** The star and the system's other worlds are drawn
     on a dome at their true directions with real relative sizes. Only the
     **sunlit part** of a world is drawn, in one flat tone, as a true phase
     (full opposite the star, a crescent near it, bright limb toward the
     star) — like the Moon, the unlit part is not visible, and has no
     outline. They rise and set behind the ranges and the terrain; the sun's
     disc reaches the horizon when the terminator reaches you.

7. **Don't:** fine noise grain, hatching/stippling, gradients inside terrain or
   ridge shapes, more than ~6 simultaneous hues in a scene.

## Where it lives

- Per-world palettes: `PlanetLook` in [planet.h](../src/world/planet.h), filled
  per archetype by `applyArchetype` in
  [system_gen.cpp](../src/world/system_gen.cpp). The archetypes swap the
  colors; the rules above stay fixed.
- Terrain banding + cel steps: `kBodyFrag` in [main.cpp](../src/core/main.cpp).
- Sky, horizon haze, soft limb: `kAtmoFrag` in main.cpp (`horizonHaze` knob).
- Ridge layers: the `specs[]` table in [planet.cpp](../src/world/planet.cpp)
  (alpha/skyMix per layer; tints from the look) + `drawRidges` in main.cpp
  (dusk/night grading, the black star mask, and the stencil that makes the
  nearest range own each pixel and keeps `beginBehindRidges()` draws out).
- Ground ellipses: `drawBlobShadow` (main.cpp), beam splat (lander.cpp).
- Depth stack (main.cpp): `drawSkyBodies`, `drawFarClouds`, `drawRidges`,
  `drawLightShafts`, `drawTerrainShadows`, `NearField::draw` (harvestables in
  depth, data in `Planet::harvest`; [nearfield.cpp](../src/world/nearfield.cpp)),
  `drawForeground` (screen-anchored planes + their props; `foreground` knob),
  `drawMotes`, `drawVignette` (`vignette` knob).
- Framing: `updateView` in main.cpp (`cameraHorizon` knob).
