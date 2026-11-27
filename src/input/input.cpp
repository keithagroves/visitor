#include "input.h"
#include <emscripten.h>
#include <cstdio>
#include <cstring>

namespace Input {

static bool keys[256] = {false};
static double wheelDelta = 0;
static float mouseX = -1.0f, mouseY = -1.0f;
static bool mouseDown = false;
static bool clicked = false;     // pressed since the game last asked (the light sword's swing)
static bool rightDown = false;   // the secondary button: reserved for interacting (chests, later)
static bool rightClicked = false; // ...pressed since the game last asked

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
        clicked = true;
    } else if (e->button == 2) {
        mouseX = (float)e->targetX;
        mouseY = (float)e->targetY;
        rightDown = true;
        rightClicked = true;
    }
    return EM_FALSE;
}

// Release is caught on the whole window, so letting go outside the canvas
// (over the HUD, off the page) can't leave the button stuck down.
static EM_BOOL mouseUpCallback(int eventType, const EmscriptenMouseEvent* e, void* userData) {
    if (e->button == 0) mouseDown = false;
    if (e->button == 2) rightDown = false;
    return EM_FALSE;
}

static EM_BOOL wheelCallback(int eventType, const EmscriptenWheelEvent* e, void* userData) {
    wheelDelta += e->deltaY;
    return EM_TRUE;  // swallow so the page doesn't scroll
}

// A text field on the page has the keyboard (the title screen's name box):
// the game leaves keys alone, so typing works and doesn't steer the ship.
static bool typing() {
    return EM_ASM_INT({
        var a = document.activeElement;
        return a && (a.tagName === 'INPUT' || a.tagName === 'TEXTAREA') ? 1 : 0;
    }) != 0;
}

static EM_BOOL keyDownCallback(int eventType, const EmscriptenKeyboardEvent* e, void* userData) {
    if (typing()) return EM_FALSE;
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
bool takeClick() { bool c = clicked; clicked = false; return c; }
bool isRightMouseDown() { return rightDown; }
bool takeRightClick() { bool c = rightClicked; rightClicked = false; return c; }

float getWheelDelta() { return (float)wheelDelta; }
void resetWheelDelta() { wheelDelta = 0; }

} // namespace Input

// ---- Touch (the page's on-screen controls, shell.html) ----------------------
// A phone has no keyboard or mouse: its buttons press the same keys and its
// finger on the world is the mouse, so the game reads one input either way.
extern "C" {
EMSCRIPTEN_KEEPALIVE void inputKey(int keyCode, int down) {
    if (keyCode >= 0 && keyCode < 256) Input::keys[keyCode] = down != 0;
}
// phase: 0 = moved, 1 = pressed, 2 = let go. x, y in canvas pixels.
EMSCRIPTEN_KEEPALIVE void inputPointer(float x, float y, int phase) {
    Input::mouseX = x;
    Input::mouseY = y;
    if (phase == 1) { Input::mouseDown = true; Input::clicked = true; }
    else if (phase == 2) Input::mouseDown = false;
}
// Pinch and the zoom buttons: the same units as the mouse wheel (+ = out).
EMSCRIPTEN_KEEPALIVE void inputWheel(float dy) { Input::wheelDelta += dy; }
}
