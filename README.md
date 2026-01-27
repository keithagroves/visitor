# Infinite Terrain Explorer

A procedurally generated 3D terrain explorer built with C++ and OpenGL ES, compiled to WebAssembly for the browser.

## Demo

First-person exploration of an infinite procedural world with:
- Mountains, valleys, and rivers generated with Perlin noise
- Dynamic chunk loading/unloading as you move
- Animated water with underwater effects
- Height-based terrain coloring (water, sand, grass, rock, snow)
- Atmospheric fog

## How It Works

```
C++ / OpenGL ES 3.0          Emscripten (emcc)           Browser
       │                           │                        │
       ▼                           ▼                        ▼
┌─────────────────┐         ┌─────────────┐         ┌──────────────┐
│  src/**/*.cpp   │         │             │         │  game.wasm   │
│  src/**/*.h     │ ──────► │   emcc      │ ──────► │  game.js     │
│  (OpenGL ES)    │         │   compiler  │         │  game.html   │
└─────────────────┘         └─────────────┘         └──────────────┘
                                                           │
                                                           ▼
                                                    ┌──────────────┐
                                                    │  WebGL 2.0   │
                                                    │  (browser)   │
                                                    └──────────────┘
```

| Source | Target |
|--------|--------|
| C++ | WebAssembly |
| OpenGL ES 3.0 | WebGL 2.0 |
| GLSL ES 300 | WebGL 2 shaders |

## Controls

| Input | Action |
|-------|--------|
| Click canvas | Enable mouse look |
| Mouse | Look around |
| W / Up | Move forward |
| S / Down | Move backward |
| A / Left | Strafe left |
| D / Right | Strafe right |
| Space | Move up |
| Shift | Move down (go underwater!) |
| ESC | Release mouse |

## Project Structure

```
src/
├── core/
│   └── main.cpp              # Application entry point and game loop
├── graphics/
│   ├── shader.h/cpp          # Shader compilation utilities
│   └── shaders.h             # GLSL shader source code
├── input/
│   ├── camera.h/cpp          # First-person camera
│   ├── crosshair.h/cpp       # HUD crosshair
│   └── input.h/cpp           # Keyboard/mouse handling
├── math/
│   ├── vecmath.h             # Vec3, Mat4 math types
│   └── noise.h               # Perlin noise generator
└── world/
    ├── chunked_terrain.h/cpp # Infinite terrain chunk system
    ├── terrain.h/cpp         # Single terrain (legacy)
    └── water.h/cpp           # Animated water plane
```

## Building

### Prerequisites

1. **Emscripten SDK**
   ```bash
   git clone https://github.com/emscripten-core/emsdk.git
   cd emsdk
   ./emsdk install latest
   ./emsdk activate latest
   source ./emsdk_env.sh
   ```

2. **CMake** (optional, for IDE support)
   ```bash
   brew install cmake  # macOS
   ```

### Build & Run

```bash
# Activate Emscripten (if not already)
source ./emsdk/emsdk_env.sh

# Build
./build.sh

# Run local server
cd build && python3 -m http.server 8080

# Open browser
open http://localhost:8080/game.html
```

> Note: You must serve via HTTP server. `file://` URLs won't work due to CORS restrictions on WASM.

## Technical Details

### Terrain Generation (CPU)

- **Chunked system**: 128x128 world unit chunks with 64x64 vertex grids
- **Perlin noise**: Multi-octave noise for varied terrain features
- **Rivers**: Domain-warped noise creates natural meandering paths
- **Dynamic loading**: Chunks load/unload based on camera position (5 chunk render distance)

### Shaders (GPU)

- **Terrain**: Height-based coloring, diffuse + specular lighting, distance fog
- **Water surface**: Animated sine waves, fresnel reflection
- **Underwater surface**: Caustic patterns, light rays from above
- **Underwater overlay**: Screen-space distortion, vignette, caustic effects

### Key Parameters

| Parameter | Value | Location |
|-----------|-------|----------|
| Chunk size | 128 units | `chunked_terrain.h` |
| Chunk vertices | 64x64 | `chunked_terrain.h` |
| Mountain height | 80 units | `chunked_terrain.h` |
| Render distance | 5 chunks | `chunked_terrain.h` |
| Camera height | 1.8 units | `camera.h` |
| Move speed | 6 units/sec | `camera.h` |

## OpenGL ES 3.0 / WebGL 2.0 Notes

- Use `#version 300 es` in shaders
- GLSL ES requires precision qualifiers: `precision mediump float;`
- Use `in`/`out` instead of `attribute`/`varying`
- Fragment shader outputs to `out vec4 fragColor` instead of `gl_FragColor`
- VAOs are required (unlike WebGL 1.0)

## Useful Emscripten Flags

| Flag | Purpose |
|------|---------|
| `-s USE_WEBGL2=1` | Enable WebGL 2.0 |
| `-s FULL_ES3=1` | Full OpenGL ES 3.0 support |
| `-s ALLOW_MEMORY_GROWTH=1` | Dynamic memory allocation |
| `-s ASSERTIONS=1` | Debug assertions (dev only) |
| `-O2` / `-O3` | Optimize for release |
| `--preload-file dir@/mount` | Bundle assets |

## Resources

- [Emscripten Documentation](https://emscripten.org/docs/)
- [OpenGL ES 3.0 Reference](https://registry.khronos.org/OpenGL-Refpages/es3.0/)
- [WebGL2 Fundamentals](https://webgl2fundamentals.org/)

## License

MIT
