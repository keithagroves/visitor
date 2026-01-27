#include "input.h"
#include <cstdio>
#include <cstring>

namespace Input {

static bool keys[256] = {false};
static bool pointerLocked = false;
static float mouseDeltaX = 0;
static float mouseDeltaY = 0;

static EM_BOOL keyDownCallback(int eventType, const EmscriptenKeyboardEvent* e, void* userData) {
    if (e->keyCode < 256) keys[e->keyCode] = true;
    if (e->keyCode == KEY_ESCAPE) {
        emscripten_exit_pointerlock();
    }
    return EM_TRUE;
}

static EM_BOOL keyUpCallback(int eventType, const EmscriptenKeyboardEvent* e, void* userData) {
    if (e->keyCode < 256) keys[e->keyCode] = false;
    return EM_TRUE;
}

static EM_BOOL mouseCallback(int eventType, const EmscriptenMouseEvent* e, void* userData) {
    if (pointerLocked) {
        mouseDeltaX += e->movementX;
        mouseDeltaY += e->movementY;
    }
    return EM_TRUE;
}

static EM_BOOL pointerLockChangeCallback(int eventType, const EmscriptenPointerlockChangeEvent* e, void* userData) {
    pointerLocked = e->isActive;
    if (pointerLocked) {
        printf("Pointer locked - use mouse to look, WASD to move, ESC to release\n");
    } else {
        printf("Pointer released - click canvas to re-enable\n");
    }
    return EM_TRUE;
}

static EM_BOOL clickCallback(int eventType, const EmscriptenMouseEvent* e, void* userData) {
    if (!pointerLocked) {
        emscripten_request_pointerlock((const char*)userData, true);
    }
    return EM_TRUE;
}

void init(const char* canvasId) {
    memset(keys, 0, sizeof(keys));

    emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, nullptr, true, keyDownCallback);
    emscripten_set_keyup_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, nullptr, true, keyUpCallback);
    emscripten_set_mousemove_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, nullptr, true, mouseCallback);
    emscripten_set_pointerlockchange_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, nullptr, true, pointerLockChangeCallback);
    emscripten_set_click_callback(canvasId, (void*)canvasId, true, clickCallback);

    printf("Input system initialized\n");
}

bool isKeyDown(int keyCode) {
    return keyCode < 256 && keys[keyCode];
}

bool isPointerLocked() {
    return pointerLocked;
}

float getMouseDeltaX() {
    return mouseDeltaX;
}

float getMouseDeltaY() {
    return mouseDeltaY;
}

void resetMouseDelta() {
    mouseDeltaX = 0;
    mouseDeltaY = 0;
}

} // namespace Input
