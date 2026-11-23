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
| Mouse wheel or + / − | Zoom |
| R | Restart after a crash |

**On foot**

| Input | Action |
|---|---|
| A / D | Walk (and steer in the air) |
| W | Jump |
| Space | Jetpack |
| Hold mouse button | Fire your tool at the cursor |
| Q | Switch between the mining laser and the terrain cutter |
| E | Trade, when standing at a trader's stall |
| Tab or I | Inventory |

### URL options

- `?seed=1234` plays a specific solar system. Without it you return to your
  last system, and on your first visit you get a random one.
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
shell.html      the page around the game: HUD, inventory, trading UI, debug panel
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
