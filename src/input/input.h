#pragma once

#include <emscripten/html5.h>

namespace Input {

// Key codes
enum Key {
    KEY_W = 87,
    KEY_A = 65,
    KEY_S = 83,
    KEY_D = 68,
    KEY_Q = 81,
    KEY_E = 69,
    KEY_SPACE = 32,
    KEY_SHIFT = 16,
    KEY_ESCAPE = 27,
    KEY_UP = 38,
    KEY_DOWN = 40,
    KEY_LEFT = 37,
    KEY_RIGHT = 39
};

void init(const char* canvasId);

bool isKeyDown(int keyCode);
bool isPointerLocked();

float getMouseDeltaX();
float getMouseDeltaY();
void resetMouseDelta();

} // namespace Input
