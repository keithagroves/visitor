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
   look. Don't posterize them. The cloud band stops a little above the
   ground (`gDebug.cloudGroundClear`, 200 units): the air the walking player,
   the posts and a parked ship stand in is clear, so cloud never washes the
   top half of a figure whose feet are on the ground.

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
   - **Down there, it is dark.** (The user, 2026-11-25: caves "only lit in
     your immediate surroundings and more by your light sword".) Once the
     player is down a hole, everything below the ground line goes dark with
     depth — near-black blue, the one place the game goes nearly black —
     except soft pools round the lights: a small one round the player (the
     suit lamp), a wider one in the sword's colour while it is drawn
     (flaring on a cut), the glowing crystals and glowcaps, rock still hot
     from the cutter, the laser where it lands, and big warm red ones round
     flares. Each pool is brightest at its light and falls off smoothly all
     the way out; no hard rims. From the surface, caves look as above.
   - **Buried ore is a lump in the rock with the ore set in it.** The lump
     is the rock around it a step darker (a shaded tone and a lit facet,
     never as dark as a hole, or it reads as one). Each kind has its own
     shape, big enough to read at a glance, in three flat tones (shaded,
     lit, and one bright facet): gold is chunky rounded nuggets, ferrite is
     tilted cubes (pale top, mid face, rust face), the world's crystal a fan
     of shards with pale tips rooted in the lump. A clump shows exactly as
     many pieces as it yields. One four-point glint twinkles on a piece at a
     time; crystal has one smooth halo, gold a fainter one. The ore keeps
     its own colors at any hour, so a find reads even at night.
   - **Mining takes ore apart; the cutter burns it.** Under the mining
     laser only the piece being worked whitens and shudders, then comes
     loose and flies to the player as faceted chips of its own color; the
     beam's glow stays small so the pieces are what you watch. Under the
     cutter the whole clump heats instead: the ore goes orange, then red in
     a lump of dull red rock, then yellow-hot just before it bursts into
     embers and cinders, with one smooth red halo that grows with the heat.
     It cools back through the same colors when left alone.
   - **Water is flat fields at one level.** A world's water stands at a
     single level right round it, so the low ground fills into ponds, lakes
     and seas. Three flat tones by depth below the surface — the shallows, a
     middle tone from about 90 units down, deep water from about 215 — the
     lines between them wandering a little, in the world's own `water` /
     `waterDeep` colors and the hour's light, like the ground. A pond is all
     shallows; only a sea shows all three. No grain, no gradient, no
     reflection of the scene.
   - **Its surface is seen at the same low angle as the ground.** The level
     itself is the FAR edge of the surface, and where the surface meets the
     walking line is 13 units lower. The strip between is the far part of
     the surface, behind whoever stands in the water: paler (toward the
     sky's haze), with a few short glints drifting along it. From the lower
     line down the water is in front of them and tints them (`waterTint`
     knob), with one thin pale line where the surface crosses them. The
     lake bed in front of the walking line shows through the water standing
     on it — tinted more the deeper the water, not at all at the shore. The
     only motion is a slow swell of a unit or two.
   - **A flooded hole takes ONE tone.** Water in a hole or tunnel is laid
     over the hole's back wall, thicker than in open water so it still reads
     as water, in a single tone whatever the depth: a tone line across a
     narrow shaft reads as a second water surface part-way down it.
   - **Spray, bubbles and rings are flat and pale.** Drops thrown up where
     something goes in or exhaust hits the water; bubbles from a helmet, a
     jetpack or the cutter under water (which boils the cut instead of
     burning it: no embers, no smoke down there); rings spreading on the
     surface, outlines only, flatter than the ground ellipse (40% of their
     width) because water is seen at a lower angle across it.
   - **Exhaust is gas, and gas is soft.** The ship's flame and smoke are a
     fluid simulation, and like the sky and the clouds they are smooth, not
     posterized: the fire is emitted light on one continuous ramp (a pale
     yellow core at the nozzle, through orange, to a red edge — a short,
     tight jet, and see-through: what is behind it shows; the debug panel's
     `flameBlue` turns it blue-white), the smoke is one pale tone in the
     hour's light — warm where the fire is beside it — and only
     as thick as there is smoke in the way. Most smoke is what the jet kicks
     up near the ground: a pale grey cloud at lift-off, only a thin, white
     trail up in the sky. No outlines, no stepped
     bands, and no straight edges: what the ship leaves behind thins away
     before the simulated patch ends. It is drawn behind the ship, so it
     never hides the hull. No air, no smoke: over an airless world there is
     only the flame. The jet meets the ground where the ship's shadow lies,
     below the silhouette line (the ground-plane rule above): it goes on
     down past the outline and blooms on the ground in front of it. On the
     pad, before lift-off, the flame flares and runs out from under the ship
     both ways along the ground.
   - **Each world's people are one kind, in the world's colors.** Flat
     vector figures like the astronaut and the cows: a lit skin tone and
     its shade, dark feet and pupils, pale eyes, and ONE accent — the
     world's crystal color, as a deeper dye — for what they wear (a sash, a
     scarf, a belt). Skin is any hue but the ground's or that accent's, so
     they read against both. Shorter and rounder than the astronaut, who
     must stay the one white figure in the scene. Where there is no air
     they wear a glass bubble. They are in the hour's light like everything
     on the ground (lifted at night, as the plants are).
   - **A trading post is built in the same few colors.** The hut is the
     world's own pale rock (its crest tone, shaded with the one below);
     poles, counter and crates are one dark timber and one plank tone; the
     awning's stripes, the cloth on the counter, the band round the hut and
     the pennant are the accent dye beside a pale cloth. Pieces are flat
     cutouts standing on the strip of ground behind the walking line, each
     rooted where the ground is (no parallax), bases flat. No new hues.
   - **Lamps are emitted light.** The lantern on a post's mast and the
     hut's window keep their color at any hour and have one smooth halo
     each, faint by day and bright at night. The lantern's halo never
     shrinks below a few pixels, so it is what marks a post from the air.
   - **Every player wears one colour** (`Livery`, src/entities/livery.h):
     a single hue for the suit's cloth (a pale tint), its trim (a strong
     tone), the chest light and visor glint, and the ship's wings, hull
     bevel and lights (dark metal with the hue in it). The helmet stays
     white and the hull's body stays dark, so every player still reads as
     the same kind of astronaut and ship. Beams keep their tool's colours.
     Online the server hands out hues at least 30 degrees apart among the
     players on at once; name tags carry the colour as a stripe.
   - **The scanner draws over everything, briefly.** A thin pale-teal ring
     sweeps out from the player, and each clump of ore it passes shows as a
     flat diamond in the ore's own color with one white facet, never less
     than a few pixels, blinking gently and gone after eight seconds. It is
     the one thing allowed in front of the near ground: it is a readout,
     not part of the scene.
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
- Water: `kWaterFrag` / `drawWaterPass` in main.cpp (three runs: behind the
  crust, in front of the walker, over the bed in front); colors in
  `PlanetLook::water` / `waterDeep`; spray, bubbles and rings are particle
  modes 4–6 (`updateWaterFx`, `drawLaserFx`).
- Ridge layers: the `specs[]` table in [planet.cpp](../src/world/planet.cpp)
  (alpha/skyMix per layer; tints from the look) + `drawRidges` in main.cpp
  (dusk/night grading, the black star mask, and the stencil that makes the
  nearest range own each pixel and keeps `beginBehindRidges()` draws out).
- Ground ellipses: `drawBlobShadow` (main.cpp), beam splat (lander.cpp).
- The locals and their posts: [outposts.cpp](../src/world/outposts.cpp)
  (`Planet::Folk` is a world's kind, set in `Planet::foundOutposts`;
  `drawAlien`, `Outposts::draw`).
- Depth stack (main.cpp): `drawSkyBodies`, `drawFarClouds`, `drawRidges`,
  `drawLightShafts`, `drawTerrainShadows`, `NearField::draw` (harvestables in
  depth, data in `Planet::harvest`; [nearfield.cpp](../src/world/nearfield.cpp)),
  `drawForeground` (screen-anchored planes + their props; `foreground` knob),
  `drawMotes`, `drawVignette` (`vignette` knob).
- Framing: `updateView` in main.cpp (`cameraHorizon` knob).
