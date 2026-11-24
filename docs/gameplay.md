---
name: gameplay
description: The game design — what the player is trying to do, the loop, the decisions that turn the sandbox into a game (goal, gates, suit meter, pack and hold, what failing costs, saving), the equipment ladder and what every resource is for, and the order to build it in. The reference every gameplay feature is checked against.
tags: [design, gameplay, progression, economy, reference]
---

# Gameplay — Visitor

Status: decided 2026-11-22; **all eight build steps built 2026-11-23**
(see *Build order* for what each step became and *Tuning* for what has not
been played through yet). If a decision turns out wrong in play, change it
here first, then in the code. Numbers are first passes to be tuned by
playing. The **decisions** are the part that should not drift.

## Where we are

Everything in the game today is a good toy and none of it is a game yet. You
can fly, land, walk, mine, dig, find caves and ore, and trade with the locals
— but nothing asks you to, nothing stops you, and nothing is at stake except
a crash that wipes the lot. The upgrades for sale make things 30% faster and
do not let you do anything you could not already do. There is no reason to
visit a second world.

A game needs four things this is missing: a **goal**, **walls** that
equipment gets you past, **limits** that make a trip a decision, and
**failure** you can recover from.

## The game in one paragraph

You are the Visitor: one small ship, a suit and a hand tool, dropped into a
star system that is not yours. The ship cannot leave it. The **star drive**
that can is built from a piece of every kind of world here. So you work
outward — the home world's surface, then under it, its moon, the near
planets, the hostile ones — and each place gives you the material for the
equipment that makes the next place possible. Build the drive, jump to the
next star, and do it again somewhere stranger.

## The loop

1. **Land** somewhere worth it. (Landing is the skill. It stays hard.)
2. **Go out on foot** for as long as the suit lasts and as far as you dare
   from the ship.
3. **Take what your tool can take**, until the pack is full.
4. **Get it back**: to the ship's hold, then to a trading post.
5. **Sell** the surplus for credits; **keep** the key materials.
6. **Upgrade**: the trader builds equipment from materials plus credits.
7. The upgrade **opens a place you could not work before**. Go there.

Every decision below exists to make one of those seven steps a choice.

## The decisions

**1. The goal is to leave.** Build the star drive, jump to the next system.
That is the win, and it is also how the "whole universe" arrives: a system is
a chapter, the jump is the chapter break, and equipment carries over.
*Not:* a score to chase, a quota of cows, an endless sandbox with no end.

**2. Progress is equipment, and equipment is never lost.** Not in a crash,
not when the suit fails, not on a jump. What you have built is the save file
that matters. *Not:* the current restart-from-nothing on a crash.

**3. Traders build upgrades from materials plus credits. There is no crafting
menu.** One panel does selling, buying and building. Every tier's key
material comes from the next place out, so credits alone never buy past the
first tier: you cannot farm grass at home and buy your way to the end.
*Not:* a crafting grid, recipes with intermediate parts, a workbench.

**4. Three pieces of equipment, three walls.** Each answers one question, and
that is the only thing it gates:

| Equipment | The question | The wall |
|---|---|---|
| **Ship** | Where can you go? | **Range** — the tank decides which worlds one hop reaches |
| **Suit** | How long can you stay, how much can you carry? | **Time** — the suit meter on hostile worlds; the pack's size |
| **Tool** | What can you take? | **Hardness** — the cutter opens the underground; gold needs a better laser |

**5. On foot you run on a suit meter.** One bar. It does not move on worlds
with good air (the home world, jungle worlds); everywhere else it drains, at
a rate set by the kind of world. It refills in the ship and at a trading
post, and you can top it up in the field by burning carbon — plants are air.
When it runs out you black out and wake in the ship without your pack. This
is what makes worlds *different places to be* rather than different colors,
and what makes "how close can I land to that?" matter.
*Not:* separate health, oxygen, heat, cold and radiation bars; hunger.

**6. The pack is small, the hold is the bank.** The suit carries a limited
number of items. Boarding the ship empties the pack into the hold (carbon
stays with you — it is your air). A trader deals with the **whole hold** if
the ship is parked within about 600 units of the post, and only with your
pack if it is not: a good landing saves you ten walks.
*Not:* the bottomless inventory there is now.

**7. Failing costs what you are carrying, never what you have built.**
Crash, or run out of fuel adrift and call for rescue: the hold and the pack
are gone, and you are set down with a ship at the last post you traded at.
Suit runs out: the pack is gone, you wake in the ship. Credits and equipment
are safe. The stake on every trip is the trip. *Not:* permadeath; a fee;
(yet) flying back to salvage your wreck.

**8. The game saves itself.** On landing and on every trade. Loading puts
you back parked where you last landed, with everything you own. What you dug
is not saved in the first version; what you built is.

**9. One currency. Score is gone.** Credits are the only number. Charting a
world and landing on it first pay credits (the locals buy your survey), in
place of the old score bonuses.

**10. Fuel stays free on the ground.** The tank refills while landed, as
now. Fuel is a limit on one hop — can I get there and get down — not a thing
to buy or mine. Landing is already hard; being broke and stranded is not the
kind of hard we want. *Revisit* once the loop is fun.

**11. The terrain cutter is the first thing you buy.** You start with the
mining laser only. Natural caves are the way underground until the cutter;
it should be yours inside ten minutes. (The current build hands it out.)

**12. Every system is winnable by construction.** Recipes never name a
crystal a system might not have. They ask for the *home crystal*, the *moon
crystal* (the home world always has a moon), and *far crystals* — any kinds
from the system's other planets. The generator guarantees at least two kinds
of far crystal; the star drive asks for some of *every kind the system has*,
and the panel lists them by name.

**13. Landing stays yours.** No upgrade in the first version makes landing
easier. What you buy changes where you can land and how long you can stay,
not how well you fly.

**14. An objective line, not a quest system.** One line on the HUD that
always says the next rung ("Sell something at the trading post", "Bring
back moon crystal"). No dialogue trees, no quest log, no story.

**15. No combat, no building, in the first version.** Nothing shoots at you
and you place nothing in the world. Both are real directions (see *Parked*)
and both would double the size of the game before the loop is proven.

## Worlds: why you cannot just walk around

The suit meter (decision 5) drains by the kind of world. Times are for a
stock suit; each level of **Seals** adds the stock time again (2x, 3x, 4x).

| Kind of world | What is wrong with it | Stock suit lasts |
|---|---|---|
| Temperate (home), Jungle | nothing | forever |
| Barren (the moons) | no air | about 3 minutes |
| Ice | cold, worst in the dark | 4 minutes in sun, 90 seconds in the dark |
| Desert | heat, only in the sun | 90 seconds in sun, forever at night |
| Exotic | the air itself | 90 seconds |
| Volcanic | heat, everywhere | 1 minute |

Two rules make this more than a timer:

- **Heat follows the sun.** Worlds here do not spin, so day and night are
  places, and the line between them creeps round as the world orbits. On a
  desert you land on the night side and work until sunrise reaches you; on
  an ice world you chase the light. The sky already tells you which is which.
- **Underground is shelter.** In a cave or a tunnel you cut yourself, heat
  and cold drain at half rate. Digging in is a way to stay longer. (It does
  nothing for no air or bad air.)

## Equipment

Everything you can own, and nothing else:

| | Upgrade | Levels | What a level does |
|---|---|---|---|
| **Ship** | Fuel tank | 3 | stock 160; then 1.5x / 2.2x / 3x: the moon comfortably, then the near planets, then anywhere (see *Tuning*) |
| | Cargo bay | 3 | +100 items in the hold and one more cow stall |
| | **Star drive** | 1 | the jump |
| **Suit** | Seals | 3 | suit time 2x / 3x / 4x |
| | Pack | 3 | carries 40 items stock, then 70 / 100 / 140 |
| | Jetpack cell | 3 | +40% jetpack fuel each |
| **Tool** | Terrain cutter | 3 | the cutter itself (dig, Q); then +35% / +75% cutting speed |
| | Mining laser | 2 | you start with I; II mines **gold** and works 40% faster; III twice as fast |
| | Scanner | unlock | a pulse that shows buried ore nearby for a few seconds |

## The ladder

Where each tier's key material comes from, and what building with it opens.
Any trader builds anything you have the materials for.

| Tier | Key material | Where it is | What it builds | What that opens |
|---|---|---|---|---|
| 0 | ferrite | home, on the ground | **Terrain cutter** | the underground |
| I | calcite, glowcap, home crystal | home: caves, ground | Seals I, Tank I, Pack I, Jetpack I, Cargo I, cutter speed | **the moon** |
| – | moon crystal | the moon | **Laser II**, **Scanner** | **gold** |
| II | gold | deep, on any world | Tank II, Seals II, Pack II, Jetpack II, Cargo II | **the near planets** |
| III | far crystals, two kinds | other planets | Tank III, Seals III, Laser III, Pack III, Jetpack III, Cargo III | **the far and hostile worlds** |
| ★ | every kind of crystal in the system, and gold | everywhere | **Star drive** | **the next system** |

First-pass costs: ten to twenty of the key material and credits rising from
150 (the cutter) to about 1,500 (tier III); the star drive is ten of every
crystal kind, thirty gold and 3,000 credits. The trade panel shows each
recipe with have / need counts and, for a material you have never held,
where it is found.

## What every resource is for

Nothing in the pack should be junk.

| Resource | From | Used for | Sells for |
|---|---|---|---|
| Carbon | plants | **suit air in the field** (5 carbon = a quarter of the bar) | almost nothing; a lot where nothing grows |
| Ferrite | rocks, shallow ore | the cutter, and some in most recipes | little |
| Calcite, glowcap | caves | tier I; glowcap for the scanner | modest |
| Home crystal | home world | tier I | little at home, about 3x on other worlds |
| Moon crystal | barren worlds | Laser II, scanner, tier II | the same pattern |
| Far crystals | other planets | tier III, the star drive | **the money**: carried between kinds of world |
| Gold | deep rock, cave veins (Laser II) | tier II, the star drive | a lot, everywhere |
| Cows | home world (the herd grows back) | — | first money; nearly double off-world |

Credits come mostly from carrying crystals between worlds, so the trading
that exists stays as it is: materials prove you have **been** somewhere,
credits prove you have **worked**. Every upgrade costs both.

## The first ten minutes

The objective line walks a new player up the first rungs, one at a time:

1. Land. → 2. Step out and walk to the trading post. → 3. Sell something.
→ 4. Buy the terrain cutter. → 5. Bring back calcite from a cave.
→ 6. Get Seals I. → 7. Land on the moon. → 8. Bring back moon crystal.
→ 9. Mine gold. → 10. Reach another planet. → 11. Collect every kind of
crystal (n of N). → 12. Build the star drive. → 13. Jump.

Pacing targets for someone playing for the first time: first sale by
minute 3, the cutter by minute 8, a purchase every three to five minutes
after that, the moon by minute 25, another planet by minute 45, the jump at
ninety minutes to two hours. A second system should take about half that.

## Where the ideas come from

Borrowed, and what each one is here:

| Idea | From | Here |
|---|---|---|
| A hazard meter that sets how long you can be outside, recharged with common stuff you pick up | No Man's Sky | the suit meter; carbon |
| A drive you must build to reach the next star, so each system is a stop on a journey | No Man's Sky | the star drive and the jump |
| Trading posts; things worth more where they are rare | No Man's Sky | built |
| Being paid for discoveries | No Man's Sky | charting pays credits |
| A scanner that shows what is under the ground | No Man's Sky | the scanner |
| Tool tiers: you can see the good ore before you can mine it | Minecraft | gold, visible from the first tunnel, needs Laser II |
| Deeper is richer | Minecraft | built (gold from about 150 down) |
| You lose what you carried, wake at your bed | Minecraft | decision 7; the last post is the bed |
| Dig in to survive the night | Minecraft | underground is shelter |
| A small inventory and a chest at home | both | the pack and the hold |
| Fuel as a range limit, landing as the risk | Kerbal Space Program | built |

Left behind on purpose: crafting grids and part trees, base building,
placing blocks, hunger, monsters and combat, space fights, creature
cataloguing, story missions.

## Parked

Good ideas that wait until the loop above is fun. Parked, not dropped:

- **Staying instead of leaving** — settling a world, robots that copy
  themselves and build it up over time. The natural second act: a reason to
  come *back* to a system. It needs building, which decision 15 defers.
- **Many systems that differ** — after the first jump every system is the
  same ladder with new worlds. Harder stars, rarer kinds of world, things
  only found three jumps out: once one system is worth finishing.
- ~~**Multiplayer.**~~ Built 2026-11-23 (players share a system's world —
  tunnels, mined ore, cut plants, trader stock — and see each other; no
  collisions, no combat, own progress each). Whether players can attack
  each other is undecided. Everyone shares one universe (2026-11-23, the
  user: "everyone uses the same universe by default. They can land on
  different planets/locations though. It makes sense to be within reasonable
  distance to alien bases"): a new game starts in the same first system
  (seed 1) for all, dropping in near one of the home world's trading posts
  picked at random, about 400 to 750 units to one side. Players
  start apart but always with a trader close by. A jump's next system depends
  only on the system jumped from, so everyone who leaves a system arrives in
  the same next one. Starts stay on the home world (my call, unconfirmed):
  the stock suit and tank are tuned for it. Online is the default (the
  user: "play online should be the default. We can add the other options
  later if we want them"): the title is a name and Play, with no New game
  and no offline choice for now.
- **Salvaging your wreck** — fly back to where you crashed and beam the
  cargo out of it.
- **Fuel you have to find**, parachutes and heat shields for worlds with
  air, landing gear — more ship equipment, once the suit and tool are right.
- **Things that live on other worlds** — to abduct, to avoid, to sell.

## Keys

Ship: W thrust, A/D turn, Space tractor beam, E step out, M map, **T time
warp** (open space only), **J jump** (star drive, clear of every world), **R
rescue** (after a crash, or adrift with an empty tank). On foot: A/D walk,
W jump, Space jetpack, mouse fires the tool, Q laser / cutter, **F breathe
carbon**, **C scan**, E trade / board, Tab pack and hold.

## Tuning: what is measured and what is not

- **The tank wall is soft.** Measured with the lander's own physics (a
  native harness, straight-up burns from the home surface): ~83 fuel to
  climb to the moon's distance, ~89 to escape the home world, ~40 more to
  land in another planet's gravity well, 4-8 for a careful landing. The
  stock tank is 160: the moon with room to spare, not another planet in one
  hop. But the tank refills on any landing, and the moon sits high in the
  home world's gravity well, so a good pilot can hop home -> moon -> planet
  on the stock tank. That is a skill shortcut, not a bug; the suit and the
  recipes still gate what you can do when you get there.
- **Never played through end to end.** Every step is tested on its own in
  the browser (title, recipes, pack and hold, suit and blackout, gold wall,
  scanner, crash and rescue, warp, jump into a new system with everything
  kept). The pacing targets above (first sale by minute 3, the jump at
  ninety minutes to two hours) have not been checked against a real cold
  start. That is the next job.

## Build order

Each step leaves the game playable. All built 2026-11-23.

1. **Keep what you build.** Save and load; a crash costs cargo and puts you
   at the last post; score becomes credits for charting. Nothing else
   matters while progress can vanish.
2. **Turn the shop into the ladder.** Recipes cost materials; the cutter is
   bought, not given; recipes show have / need; the objective line.
3. **Pack and hold.** The pack's limit, the hold, trading from the hold
   when parked near a post, dropping things you do not want.
4. **The suit meter.** Drain by kind of world, Seals, carbon top-ups,
   refills at the ship and at posts, heat follows the sun, shelter
   underground.
5. **Tool walls.** Gold needs Laser II; the scanner.
6. **Flight as a wall.** Tune the tank so the stock ship reaches the moon
   and no further; time warp on long coasts (a hop is two minutes of
   nothing today); the herd grows back.
7. **The star drive and the jump.** Generator guarantee (two kinds of far
   crystal), the recipe that lists the system's kinds, a new seed with
   equipment kept.
8. **A front door.** Title screen, then tune everything against the pacing
   targets by playing from a cold start.

What each step became: the save is one JSON array in localStorage
(`visitor:save`, `saveText` in main.cpp), written on landing, every trade,
boarding, a crash and a blackout, and it names its own system, so the page
picks the saved seed; the recipes and the ladder are `Trade::recipe` in
trade.cpp, with materials named by role and resolved per system by
`Trade::systemOf`; the generator guarantee is in system_gen.cpp (bare-rock
planets nearest home turned into missing kinds); the jump saves the next
seed and reloads the page into it. `?notitle` in the URL skips the title
into a fresh game and drops in at the ranch (-90 degrees, by the herd) every
time; the capture rig and the tests use it.

## Open

Decided above and now built; these are the ones most worth a second opinion
after playing, because each changes how the whole game feels:

- **The suit meter** (5). The biggest change: it adds pressure to walking
  around, which is calm today. The home world stays calm either way.
- **Free fuel** (10). The safe choice. The braver one — fuel made from
  carbon — ties mining to flying and makes dead worlds dangerous to visit.
- **Trading from the hold only when parked close** (6). Rewards a precise
  landing; punishes a sloppy one with walking.
- **Leaving as the goal** (1), rather than building something where you are.
