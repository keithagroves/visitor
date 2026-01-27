cmake_minimum_required(VERSION 3.16)
project(WebGLGame)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Source files
set(SOURCES
    src/main.cpp
)

add_executable(game ${SOURCES})

# Emscripten-specific settings
if(EMSCRIPTEN)
    set(CMAKE_EXECUTABLE_SUFFIX ".html")

    # Emscripten compile and link flags
    set(EMSCRIPTEN_FLAGS
        "-s USE_WEBGL2=1"           # Enable WebGL 2.0 (OpenGL ES 3.0)
        "-s FULL_ES3=1"             # Full OpenGL ES 3.0 emulation
        "-s WASM=1"                 # Output WebAssembly
        "-s ALLOW_MEMORY_GROWTH=1"  # Allow dynamic memory growth
        "-s NO_EXIT_RUNTIME=1"      # Keep runtime alive after main()
        "-s EXPORTED_RUNTIME_METHODS=['ccall','cwrap']"
    )

    # Join flags into a string
    string(JOIN " " EMSCRIPTEN_FLAGS_STR ${EMSCRIPTEN_FLAGS})

    # Apply flags
    set_target_properties(game PROPERTIES
        LINK_FLAGS "${EMSCRIPTEN_FLAGS_STR} --shell-file ${CMAKE_SOURCE_DIR}/shell.html"
    )

    # If you have assets, uncomment this:
    # set_target_properties(game PROPERTIES
    #     LINK_FLAGS "${EMSCRIPTEN_FLAGS_STR} --preload-file ${CMAKE_SOURCE_DIR}/assets@/assets"
    # )
endif()
