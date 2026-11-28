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
menu.** One panel does selling, buying and building (two tabs: *Sell & buy*,
and *Equipment* — a tile per upgrade under ship / suit / tool). Every tier's key
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

**10. Fuel is made from carbon.** (Changed 2026-11-27: it used to refill
for free on every landing.) The ship drops in with a little over what a shaky
landing takes (20 of the stock 160) and landing no longer tops it up. **F
aboard** turns carbon into fuel, 8 a piece, as much as the tank has room for:
from the pack, then the hold, parked or adrift. Carbon comes from cutting
plants, and from crawlers and bats. So mining feeds flying, the carbon you
carry is both your air and your range, and a world with nothing growing (a
moon) is somewhere you bring your way home to. What keeps it from stranding
anyone: landed on under 6 fuel, **R calls a rescue** (as adrift with an empty
tank: the cargo is lost), and a rescue always sets you down on a world where
plants grow, with 20 fuel. Arriving in a new system after a jump starts on 20
too. The tank is saved; a bigger tank is room, not fuel.

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

**15. No building in the first version; combat is two creatures and one
weapon.** You place nothing in the world. *Revised 2026-11-25* (the user:
"We should add a light sword for attacking hostile creatures. It slashes in
any direction your mouse is pointing. We would have to add some hostile
creatures that come out at night."): the night has **crawlers**, the caves
have **bats** day and night, and you have a **light sword** to fight them
with (see *Night*). 2026-11-25, the user again: "I think the sword should be
the same button as the laser. You have to equip it when enemies are near.
Right click can be for opening chests or something. In the caves there
should be some enemies in the day. e.g. flying bats. I'm kind of going for
hallow knight vibes here." So the sword is the hand tool's third mode, and
the right mouse button is kept free for interacting with things. That is all the
combat there is: nothing shoots, there is no health bar (a bite comes off
the suit meter, decision 5), and whether players can hurt each other is
still undecided. Building is still a real direction (see *Parked*).

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

## Night (and the caves)

Worlds do not spin, so night is a place (see *Heat follows the sun*): the
half of a world facing away from the star, and the line creeps round as the
world orbits. On every world with life on it — not the airless moons —
**crawlers** come up out of the ground there after dark, and the natural
caves have **bats** whatever the hour. Built 2026-11-25; the rules below
are first passes. The user's: crawlers at night, bats in the caves by day,
a sword that cuts toward the mouse on the laser's button and has to be
drawn, right-click left for interacting, the Hollow Knight feel. The rest
is my call, not yet confirmed.

- **They come to you.** While you are out on foot where the sun is well
  down, one comes up every five to nine seconds, 430 to 650 units off to
  one side (out of shot at the ground zoom), up to two at a time at dusk
  and four in the dead of night. They stalk you along the ground a little
  slower than you run, crouch when they are close — low, jaw open: the tell
  — and pounce.
- **A bite comes off the suit meter.** 12% of a full bar per bite (about
  eight from full), with a moment after each in which another can't land.
  No health bar: the suit meter is the one bar, so a bite on a hostile
  world is worse, and running out is the same blackout (decision 7: the
  pack is gone, equipment is not).
- **Bats** hang beside about a third of the stalactites in the natural
  caves (not on the airless moons). Come down into their cave within about
  200 of one and it drops off the ceiling, circles above and to one side
  of you, holds still with its wings high and its eyes flaring — the tell —
  and dives straight at where you were. A bite is two thirds of a
  crawler's. One cut kills it; its roost stays empty for three minutes.
  Bats never fly out into the open air. Seen from the front: dark wings
  and a pale mask with dark eye holes, which is what reads in an unlit cave.
- **The light sword** is everyone's from the start (a new player can land
  at night). It is the hand tool's third mode: **Q** cycles laser → cutter
  (once bought) → sword, and **1 / 2 / 3** pick one, so you **draw it**
  when something comes for you (the game says so, at most every 25 s, when
  something is coming and the sword is not out). Drawn, it is always out:
  held lit at a guard in your hand (the user: "When your light sword is out
  it should be always out and should be brighter when it swings"). Drawing
  it ignites it like a light saber (the user: "it should extend out like a
  light saber"): the blade shoots out of the hilt in 0.22 s; putting it
  away slides it back in. Each
  click of the mouse button is one cut across the line to the cursor, any
  direction, flaring bright: 0.17 s across the arc, then 0.1 s back to the
  guard. A click during the return cuts again at once; a click during the
  cut is kept and cuts again the moment it ends (no dead time after a
  swing — the user saw a delay there). It reaches about 46 units past your
  chest and catches anything within 80 degrees either side of the aim. A
  cut knocks a crawler back; the second kills it.
- **The Hollow Knight feel.** A hit freezes the fight for an instant
  (0.06 s: you, the crawlers and the bats; the world goes on) and pushes you
  back off what you hit. Cutting **down** onto something while you are in
  the air bounces you up off it — a pogo — about as high as a jump.
- **A kill leaves carbon** — 2 from a crawler, 1 from a bat. Carbon is air
  (decision 5): fighting pays back a little of what it costs.
- **Shelter.** None come up near a trading post (450 along the ground),
  and none come nearer to its lantern than 320: a post is safe ground at
  night. None come up while you are underground (they will follow you into
  a hole they can drop into). They stop at the water's edge. In the ship
  you are out of reach; they give up after a few seconds.
- **Dawn.** When the sun reaches a crawler it digs back in.
- **They are yours alone.** Online, other players do not see your crawlers,
  your bats or your sword: not shared yet.

Tunable on the debug panel: *Night crawlers* (0 = none, 1 = as above),
*Cave bats* (0 = none, 1 = a third of the stalactites) and *bite*.

**The caves are dark** (the user, 2026-11-25: "only lit in your immediate
surroundings and more by your light sword … some material to buy some kind
of light source … maybe flares"). Underground you see only what your lights
reach: the suit lamp (a small pool), the light sword when drawn (a wider
one), what glows down there, and **flares**. Any trader makes them to order:
3 for 2 carbon, 1 ferrite and 10 credits, up to 12 carried (saved with the
game). **G** throws one toward the aim; it bounces to rest and burns red for
about half a minute, lighting a wide stretch of cave; at most 8 burn at
once. They work on the surface at night too. Calls of mine, unconfirmed:
the recipe and numbers, G, flares not shown to other players online, and a
one-time notice the first time you go down without light. Debug panel:
*Caves: darkness* (0 = off) and *+6 flares*.

**The posts shut at night** (the user, 2026-11-25: "make the towns closed at
night so you can't buy stuff at the shop"). A post closes when the sun at it
is a little below the horizon (`Outposts::CLOSE_SUN`) and opens at dawn: the
shutter comes down over the stall, a sign with a moon on it goes up, the
trader goes into the hut and the others go home. No trading at all — not
buying, not selling, not from the hold; a trade open at nightfall is closed
("... is shutting up for the night"), and E at the stall says so. Since a
world's day is its year (twenty-odd minutes at home), a night is a long
wait: the way round it is a post on the day side. My calls, unconfirmed:
the lantern stays lit and the post is still safe from crawlers; the suit
still refills at a shut stall (closing a shop should not strand anyone on
a hostile world); a new player is dropped by a post in daylight, so the
first sale is never blocked by the hour; the HUD's trader line says
"shut".

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
| | Light sword | owned | the tool's third mode (Q, or 3): a click is a cut toward the cursor (see *Night*); not upgraded yet |

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
| Carbon | plants | **suit air in the field** (5 carbon = a quarter of the bar); flares | almost nothing; a lot where nothing grows |
| Ferrite | rocks, shallow ore | the cutter, and some in most recipes; flares | little |
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
placing blocks, hunger, space fights, creature cataloguing, story
missions. (Monsters and combat were on this list until 2026-11-25: see
*Night*.)

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
  and no offline choice for now. **Transmissions** (built 2026-11-25; the
  user: "send transmissions to other players. For now it could be a global
  chat for anyone on the server"): Enter opens a box bottom left; a line
  goes to everyone on the server, whatever system they are in, tagged with
  the sender's star when it is not yours. New lines show for twelve seconds
  and fade; opening the box shows the last fifty (a newcomer gets them too).
  Text only, 200 characters, one line every 1.2 s each. No accounts, so
  names can be borrowed, and no word filter yet.
- **Salvaging your wreck** — fly back to where you crashed and beam the
  cargo out of it.
- **Fuel you have to find**, parachutes and heat shields for worlds with
  air, landing gear — more ship equipment, once the suit and tool are right.
- **Things that live on other worlds** — to abduct, to avoid, to sell.

## Keys

Ship: W thrust, A/D turn, Space tractor beam, E step out, **F make fuel
from carbon**, M map, **T time warp** (open space only), **J jump** (star
drive, clear of every world), **R rescue** (after a crash, adrift with an
empty tank, or parked on too little to lift off). On foot: A/D walk,
W jump, Space jetpack, mouse fires the tool (with the sword out, each click
is a cut), **Q laser / cutter / sword, or 1 / 2 / 3**, **F breathe carbon**,
**C scan**, E trade / board, Tab pack and hold. The right mouse button is
kept for interacting with things (chests, later). Online: **Enter
transmits** (global chat).

## Tuning: what is measured and what is not

- **The tank wall is soft.** Measured with the lander's own physics (a
  native harness, straight-up burns from the home surface): ~83 fuel to
  climb to the moon's distance, ~89 to escape the home world, ~40 more to
  land in another planet's gravity well, 4-8 for a careful landing. The
  stock tank is 160: the moon with room to spare, not another planet in one
  hop. Fuel is made from carbon at 8 a piece (decision 10), so a full tank is
  20 carbon and the moon trip about 11; the moon grows none, so the way home
  has to be carried there. Coming down from the drop takes ~2.5 with a
  perfect burn (the atmosphere does most of the braking), and 6 or so in a
  headless test of the autopilot; the ship starts with 20.
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
- **Fuel made from carbon** (10). Built 2026-11-27, in place of free fuel on
  every landing: ties mining to flying and makes dead worlds dangerous to
  visit. Worth watching how long the first trip off the home world takes.
- **Trading from the hold only when parked close** (6). Rewards a precise
  landing; punishes a sloppy one with walking.
- **Leaving as the goal** (1), rather than building something where you are.
