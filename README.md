# Visitor

A 2D space-exploration game that runs in the browser. It's written in C++ and
OpenGL ES 3.0, and compiled to WebAssembly and WebGL 2 with Emscripten.

**[Play it here →](https://keithagroves.github.io/visitor/)**

You fly a small lander through a procedurally generated solar system. Beam
cows up from the home world, then step outside in a jetpack to dig into the
crust, mine ore and explore caves. Sell what you find to the locals at their
trading posts, buy upgrades, and fly on to chart the next planet.

- **A new solar system from every seed.** One number generates the star,
  4–6 planets, their moons, and each world's colours, sky, clouds, terrain,
  gravity and name.
- **Fully destructible planets.** Each world is a huge solid body that you can
  cut into anywhere. It has natural caves, buried ore, and water that floods
  the holes you dig.
- **Real orbital flight.** Gravity falls off with distance, planets and moons
  follow Kepler orbits, and atmospheric drag lets you burn off speed and set
  down gently.
- **Computed skies.** Rayleigh and Mie atmospheric scattering, sunsets, drifting
  clouds and a day/night terminator, all drawn by full-screen shader passes.

> Visitor is still in active development. Expect rough edges and missing
> features.

## Controls

**In the ship**

| Input | Action |
|---|---|
| W / ↑ | Thrust |
| A / D | Rotate |
| Space | Tractor beam |
| E | Leave / board the ship |
| M | System map |
| T | Time warp, in open space (not online) |
| J | Jump to the next star, once you have built a star drive |
| Mouse wheel or + / − | Zoom |
| R | Call for a rescue after a crash, or when out of fuel |

**On foot**

| Input | Action |
|---|---|
| A / D | Walk (and steer in the air) |
| W | Jump |
| Space | Jetpack |
| Hold mouse button | Fire your tool at the cursor |
| Q | Switch between the mining laser and the terrain cutter (once you have one) |
| F | Breathe carbon into the suit |
| C | Scan for ore (once you have a scanner) |
| E | Trade, when standing at a trader's stall |
| Tab or I | Your pack and the ship's hold |

### URL options

- `?seed=1234` plays a specific solar system. Without it you return to your
  last system, and on your first visit you get a random one.
- `?server=ws://host:9001` picks the multiplayer server (see below). By
  default a page on your own machine uses port 9001 on the same host, and
  the published page uses the hosted server.
- `?debug` turns on the developer panel, with live tuning sliders, warps
  between worlds and new random systems. This setting is remembered on your
  browser; `?debug=0` turns it off again.

## Building

You need the [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html):

```bash
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk && ./emsdk install latest && ./emsdk activate latest
source ./emsdk_env.sh
```

Then build from the repo root, and serve the output over HTTP. Browsers won't
load WebAssembly from `file://` URLs.

```bash
./build.sh
cd build && python3 -m http.server 8080
# open http://localhost:8080/game.html
```

Every push to `main` builds and deploys the game to GitHub Pages
([.github/workflows/deploy.yml](.github/workflows/deploy.yml)).

### Playing together

Multiplayer has two interchangeable servers that speak the same messages: a
small [uWebSockets.js](https://github.com/uNetworking/uWebSockets.js) server
in [server/](server/) to run on your own machine, and a Cloudflare Worker in
[worker/](worker/) for hosting. Each star system (seed) is a room: everyone in
it shares one clock, so the planets are in the same place for all, and every
change anyone makes to the world (tunnels, mined ore and cave formations, cut
plants, a trader's stock) is kept in order and sent to everyone, including
players who join later. You see the players at the same world as you. Your
own progress (credits, equipment, what you carry) stays in your browser.

```bash
cd server && npm install && npm start      # ws://localhost:9001
```

Then press **Play online** on the title screen. To play with friends, share
a link with the same seed (`game.html?seed=1234`) and point everyone at the
same server with `?server=ws://your-host:9001`. A page served over HTTPS
(like GitHub Pages) needs a `wss://` server behind TLS. This server keeps
rooms in memory: restarting it starts every system's world afresh.

#### Hosting it for free (Cloudflare)

[worker/](worker/) is the same server as a Cloudflare Worker with one
Durable Object per star system, which fits Cloudflare's free plan. There, a
system's world is saved: it stays dug after everyone leaves and through
restarts.

```bash
cd worker && npm install
npm run dev                  # try it locally: ws://localhost:8787, no account
npx wrangler login           # once: a free Cloudflare account
npm run deploy               # wss://visitor-server.<your-subdomain>.workers.dev
```

The published game uses the one deployed at
`wss://visitor-server.team-channel.workers.dev` (`HOSTED_SERVER` in
[shell.html](shell.html); change it there for your own deployment). The free plan allows 100,000
requests a day, where 20 incoming messages count as one request. A player
moving about sends about 10 messages a second, and one standing still
sends 1. That comes to roughly 50 player-hours a day of active play,
and more when people stand around. A room is billed for the time it
is awake, and a busy room stays awake. The free plan allows about 29
room-hours of that a day, so several small groups in different systems
can reach that limit before the request limit. Past a limit, the
server stops until 00:00 UTC; nothing is charged.

## How it's put together

```
src/
├── core/       main loop, game state, camera, shader passes, trading, debug knobs
├── entities/   the lander, the astronaut, cows
├── world/      solar-system generation, planets, destructible terrain, caves,
│               ore deposits, water, trading posts
├── graphics/   immediate-mode 2D batch renderer, shader helper
├── input/      browser keyboard/mouse bridge
└── math/       Vec2/Mat3 (degree-based trig), Perlin noise
shell.html      the page around the game: title, HUD, inventory, trading UI, multiplayer client, debug panel
server/         the multiplayer server (Node + uWebSockets.js)
worker/         the same server, hosted: a Cloudflare Worker + Durable Objects
```

The architecture docs start at [docs/overview.md](docs/overview.md), and the
visual rules are in [docs/art-style.md](docs/art-style.md).

## Acknowledgements

- Visitor started as a C++ port of *cow-abductor*, an earlier p5.js game by
  the same author.
- The sky and lighting work drew on Maxime Heckel's
  [shader articles](https://blog.maximeheckel.com/) on atmospheric scattering
  and volumetric light, and on Sébastien Hillaire's
  [*A Scalable and Production Ready Sky and Atmosphere Rendering Technique*](https://sebh.github.io/publications/egsr2020.pdf).
- The art direction takes cues from *A Short Hike*.

## License

[AGPL-3.0](LICENSE). You're free to use, modify and share this code, but
anything you build on it, including a modified version you host online, must
be released under the same license with its full source.
