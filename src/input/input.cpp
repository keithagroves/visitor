#include "input.h"
#include <cstdio>
#include <cstring>

namespace Input {

static bool keys[256] = {false};
static double wheelDelta = 0;
static float mouseX = -1.0f, mouseY = -1.0f;
static bool mouseDown = false;

static EM_BOOL mouseMoveCallback(int eventType, const EmscriptenMouseEvent* e, void* userData) {
    mouseX = (float)e->targetX;
    mouseY = (float)e->targetY;
    return EM_FALSE;
}

static EM_BOOL mouseDownCallback(int eventType, const EmscriptenMouseEvent* e, void* userData) {
    if (e->button == 0) {
        mouseX = (float)e->targetX;
        mouseY = (float)e->targetY;
        mouseDown = true;
    }
    return EM_FALSE;
}

// Release is caught on the whole window, so letting go outside the canvas
// (over the HUD, off the page) can't leave the button stuck down.
static EM_BOOL mouseUpCallback(int eventType, const EmscriptenMouseEvent* e, void* userData) {
    if (e->button == 0) mouseDown = false;
    return EM_FALSE;
}

static EM_BOOL wheelCallback(int eventType, const EmscriptenWheelEvent* e, void* userData) {
    wheelDelta += e->deltaY;
    return EM_TRUE;  // swallow so the page doesn't scroll
}

static EM_BOOL keyDownCallback(int eventType, const EmscriptenKeyboardEvent* e, void* userData) {
    if (e->keyCode < 256) keys[e->keyCode] = true;
    // Swallow the event (returning true prevents arrow keys from scrolling the
    // page, etc.). We don't need the browser's default handling here.
    return EM_TRUE;
}

static EM_BOOL keyUpCallback(int eventType, const EmscriptenKeyboardEvent* e, void* userData) {
    if (e->keyCode < 256) keys[e->keyCode] = false;
    return EM_TRUE;
}

void init(const char* canvasId) {
    memset(keys, 0, sizeof(keys));
    emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, nullptr, true, keyDownCallback);
    emscripten_set_keyup_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, nullptr, true, keyUpCallback);
    // Wheel on the canvas only, so scrolling over the debug panel scrolls it
    // instead of zooming the world.
    emscripten_set_wheel_callback(canvasId, nullptr, true, wheelCallback);
    emscripten_set_mousemove_callback(canvasId, nullptr, true, mouseMoveCallback);
    emscripten_set_mousedown_callback(canvasId, nullptr, true, mouseDownCallback);
    emscripten_set_mouseup_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, true, mouseUpCallback);
    printf("Input system initialized (keyboard + wheel + mouse)\n");
}

bool isKeyDown(int keyCode) {
    return keyCode < 256 && keys[keyCode];
}

// Mouse / pointer-lock are unused in the 2D game but kept so the header API
// stays stable for callers that still reference them.
bool isPointerLocked() { return false; }
float getMouseDeltaX() { return 0.0f; }
float getMouseDeltaY() { return 0.0f; }
void resetMouseDelta() {}

float getMouseX() { return mouseX; }
float getMouseY() { return mouseY; }
bool isMouseDown() { return mouseDown; }

float getWheelDelta() { return (float)wheelDelta; }
void resetWheelDelta() { wheelDelta = 0; }

} // namespace Input
