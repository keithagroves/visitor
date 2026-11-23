#!/bin/bash

# Build script for Emscripten WebGL project

set -e

BUILD_DIR="build"
SRC_DIR="src"

if ! command -v em++ &> /dev/null; then
    echo "Error: Emscripten (em++) not found!"
    echo ""
    echo "Install Emscripten:"
    echo "  git clone https://github.com/emscripten-core/emsdk.git"
    echo "  cd emsdk"
    echo "  ./emsdk install latest"
    echo "  ./emsdk activate latest"
    echo "  source ./emsdk_env.sh"
    exit 1
fi

echo "=== Building Visitor ==="
echo ""

mkdir -p "$BUILD_DIR"

# All source files (organized by module)
SOURCES="
    $SRC_DIR/core/main.cpp
    $SRC_DIR/core/trade.cpp
    $SRC_DIR/graphics/shader.cpp
    $SRC_DIR/graphics/renderer2d.cpp
    $SRC_DIR/world/planet.cpp
    $SRC_DIR/world/system_gen.cpp
    $SRC_DIR/world/nearfield.cpp
    $SRC_DIR/world/terrain_field.cpp
    $SRC_DIR/world/carve_map.cpp
    $SRC_DIR/world/cave_gen.cpp
    $SRC_DIR/world/harvest_gen.cpp
    $SRC_DIR/world/cavefield.cpp
    $SRC_DIR/world/deposits.cpp
    $SRC_DIR/world/outposts.cpp
    $SRC_DIR/entities/lander.cpp
    $SRC_DIR/entities/cow.cpp
    $SRC_DIR/entities/astronaut.cpp
    $SRC_DIR/input/input.cpp
"

echo "Compiling..."
em++ $SOURCES \
    -o "$BUILD_DIR/game.html" \
    -std=c++17 \
    -I"$SRC_DIR" \
    -I"$SRC_DIR/core" \
    -I"$SRC_DIR/graphics" \
    -I"$SRC_DIR/world" \
    -I"$SRC_DIR/input" \
    -I"$SRC_DIR/math" \
    -s USE_WEBGL2=1 \
    -s FULL_ES3=1 \
    -s WASM=1 \
    -s ALLOW_MEMORY_GROWTH=1 \
    -s NO_EXIT_RUNTIME=1 \
    -s "EXPORTED_RUNTIME_METHODS=['ccall','cwrap']" \
    --shell-file shell.html \
    -O2

# Create index.html redirect for GitHub Pages
cat > "$BUILD_DIR/index.html" << 'EOF'
<!DOCTYPE html>
<html>
<head>
  <meta http-equiv="refresh" content="0; url=game.html">
</head>
<body>
  <p>Redirecting to <a href="game.html">game.html</a>...</p>
</body>
</html>
EOF

echo ""
echo "=== Build Complete ==="
echo ""
echo "Output files:"
ls -la "$BUILD_DIR"/*.html "$BUILD_DIR"/*.js "$BUILD_DIR"/*.wasm 2>/dev/null || true
echo ""
echo "To run:"
echo "  cd $BUILD_DIR && python3 -m http.server 8080"
echo "  Open http://localhost:8080/game.html"
