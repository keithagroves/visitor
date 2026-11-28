# Plan: a big universe

Status: **plan, not built** (written 2026-11-27). Open decisions are marked
**Decide** with a suggested default.

## Goal

A universe large enough that no one runs out of places to go, and a reason to
go there: worlds you are the first to see, and get to name.

The size stays as it is: **999,998 systems** (seeds 1–999,998, the range
`finishJump` already uses). What changes is how they are laid out and reached.
Today the star drive is a chain: each system has exactly one "next" system
(a hash of its seed). This plan replaces the chain with a **galaxy map**: every
system has a place on a map, and you jump to the one you pick, within the
drive's range.

## Why performance does not change

- **One system is loaded at a time**, as now. Its planets, terrain, caves and
  digs exist only while you are in it. A jump still saves and reloads the page
  into the new seed.
- **Nothing about the galaxy is stored.** A system's place on the map, and
  what the map says about it, come from its seed. The map draws only the cells
  on screen, and each one costs a few random numbers (see Stage 1). It does
  not run the system generator.
- **Galaxy coordinates never touch the physics.** They are whole-number cells
  on the map. Inside a system the float coordinates stay as they are (the
  ~2.1M-unit limit noted in [system_gen.cpp](../src/world/system_gen.cpp) is
  per system and unaffected).
- **The shaders' 10-body cap** (`SHADER_MAX_PLANETS`) is per system and
  unaffected.
- **What grows is the save and the server's records**, by what players change
  and name. Both are bounded (Stages 3 and 5).

## The galaxy

A 1000 × 1000 grid of cells, one system per cell (two cells left over). The
seed is the cell:

```
index = y * 1000 + x                       (0 .. 999,999)
k     = (index + 499,500) mod 1,000,000    (the offset puts k = 0 at the centre)
seed  = 1 + k, or no star if k >= 999,998  (the two cells left over are empty)
```

- **Seed 1, where everyone starts, is the centre cell (500, 500).** Every
  existing seed still exists and keeps its contents, so saves keep working:
  only the map position is new.
- A star's spot inside its cell is jittered from the seed, so the map does not
  look like a grid.
- **Decide: the galaxy's shape.** Default: every cell has a star (a square
  field). Alternative: a density map (a disc, or spiral arms) where some cells
  are empty. That looks better, but leaves fewer than a million systems and
  makes some routes impossible at low range.

## Stages

Each stage leaves the game playable.

### Stage 1 — Seed ↔ cell, and a system preview

- **Mapping:** `seedToCell` / `cellToSeed` (the formula above), with a test
  that runs both ways for every seed.
- **Preview:** `previewSystem(seed)` runs only the start of `generateSystem`'s
  random number sequence: the star's name, then the planet count, then whether
  there is an inner world. Those are its first draws, so the preview is cheap
  and always agrees with the real system.
- The preview is what the map shows: star name and colour, and how many
  worlds.

### Stage 2 — The galaxy map, and jumping where you choose

- **Opening the map:** with a star drive, clear of every world, J (or a Jump
  button on touch) opens the galaxy map, centred on where you are, instead of
  jumping straight away.
- **On the map:** stars in range are lit; the others are dim. Tapping a star
  shows its preview and Jump. Systems you have been to, and named worlds
  (Stage 5), get marks.
- **The jump itself is unchanged:** the spool-up, the white-out and the reload,
  into the seed you picked.
- **Range:** **Decide.** Default: the drive reaches 3 cells, and further drive
  upgrades reach 6, then 12. This makes range part of the equipment ladder in
  [gameplay.md](gameplay.md).
- **Does the drive still burn out each jump?** **Decide.** Default: no. The
  drive is kept, and each jump costs fuel plus a crystal instead. With a map
  you can go back, and rebuilding the drive every jump would make that
  tiresome.
- **Online:** nothing changes. Players are already grouped by system on the
  server (one room per seed), so whoever picks the same star meets there.

### Stage 3 — The save remembers systems you have left

Today the save holds one system's state (which worlds are charted and visited,
each post's stock), and a jump starts the next one fresh. Going back would
show everything uncharted again.

- **New block in the save:** a block keyed by seed for every system you have
  been to. It holds the charted and visited bits and the posts' stock, and
  later your names (Stage 4). A version bump, so old saves load with an empty
  block.
- **Cap:** the most recent 300 systems, which fits easily in localStorage.
  Older ones forget the posts' stock but keep the charted bits, which cost
  only a few bytes each.
- The map reads this block to mark where you have been.

### Stage 4 — Naming worlds (on your own)

- **Who can name:** when you chart a world no one has named (the existing
  "Discovered …" moment), the banner offers **Name it**.
- **The name box:** 3–20 characters; letters, digits, spaces, `'` and `-`.
- **Where names show:** in your HUD, the system map (M), nav labels and the
  galaxy map. The generated name stays underneath ("Halcyon · Selum IV").
- **Offline:** names live in your save (Stage 3's block).
- **Decide: what can be named.** Default: planets and moons now, and the star
  (the system's name) once you have charted every world in it. That gives a
  reason to finish a system before moving on.

### Stage 5 — Shared names (online)

So that a name is seen by everyone, and the first to arrive gets the credit.

- **Where names are kept:** a new Durable Object on the worker, **Atlas**,
  like the existing global Chat room. It is one store for every name in the
  galaxy, keyed by (seed, body), holding the name, who named it and when.
  - **First name wins.** Only the namer can rename, and only within a day.
  - The Atlas checks length and characters again, and runs a short blocklist.
- **How the map gets them:** `GET /atlas?cells=x0,y0,x1,y1` returns the names
  in a region, and the galaxy map asks for what is on screen.
- **How you see a new name:** entering a system fetches its names. The chat
  feed announces new names: *"Keith named Selum IV 'Halcyon'"*. That feed is
  the advertisement for exploring.
- **Cost:** one row written per name. The free tier's 100,000 writes a day is
  far above what naming needs.
- **Decide: moderation beyond the blocklist.** Default: a debug-only Clear
  button for names, and nothing more until it is needed.

### Stage 6 — Exploration credit (later)

- The galaxy map shows who discovered each system, and your own count:
  systems visited, worlds charted, worlds named.
- Possibly a small credit reward for each world you name, paid once.

## What this does not change

- How a system is generated, and its float coordinates. The cap of 4–6
  planets plus moons stays.
- Every system's home world is still Temperate. That is fine for now: it means
  every jump lands somewhere breathable. Making home worlds vary is a separate
  decision.
- One system in memory; a page reload between systems.

## Files it touches

- [src/world/system_gen.cpp](../src/world/system_gen.cpp): the preview, and the
  seed ↔ cell mapping (or a new `galaxy.cpp` beside it).
- [src/core/main.cpp](../src/core/main.cpp): `tryJump` / `finishJump` take a
  chosen seed; the save's systems block; the naming moment.
- [shell.html](../shell.html): the galaxy map view, the name box, and showing
  names.
- [worker/src/index.js](../worker/src/index.js): the Atlas Durable Object and
  its `/atlas` route; plus a binding and migration in `wrangler.jsonc`.
- Docs: [gameplay.md](gameplay.md) (range on the equipment ladder, naming),
  [overview.md](overview.md).
