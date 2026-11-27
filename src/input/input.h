#pragma once

#include <emscripten/html5.h>

namespace Input {

// Key codes
enum Key {
    KEY_W = 87,
    KEY_A = 65,
    KEY_S = 83,
    KEY_D = 68,
    KEY_Q = 81,   // on foot: switch tool (mining laser / terrain cutter)
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

// Accumulated wheel delta (deltaY) since the last reset, for manual zoom.
float getWheelDelta();
void resetWheelDelta();

// Pointer over the canvas, in canvas pixels (origin top-left); (-1,-1) until
// the first move. The button is the primary one, pressed ON the canvas — a
// press on the HUD / debug panel never reaches the game.
float getMouseX();
float getMouseY();
bool isMouseDown();
// Was the primary button pressed since the last call? (Clears it.) A quick
// click that is down and up again between two frames still registers: the
// light sword swings once per click.
bool takeClick();
// The secondary (right) button, pressed on the canvas — reserved for
// interacting with things (opening chests, later); nothing uses it yet. The
// page turns off the canvas's context menu so it reaches the game.
bool isRightMouseDown();
bool takeRightClick();

// ---- The two thumb sticks of the touch controls (shell.html) ---------------
// The left stick moves: it presses the same keys a keyboard would (A/D, W,
// S), and this is how far it is pushed — across (-1 left .. +1 right) and up
// (-1 down .. +1 up), each 0 when it is let go, and always 0 on a keyboard.
// The ship turns by it (a little push, a slow turn), and on foot holding it
// up once you are off the ground is the jetpack.
float stickX();
float stickY();
// The right stick points the hand tool: is a thumb on it, and which way it is
// pushed on screen (+x right, +y down; its length 0..1 is how far). The game
// turns that into a pointer of its own (setPointer), so the tools read one
// mouse whichever way it is aimed.
bool aimHeld();
float aimX();
float aimY();
// Has either stick been used this session (the player is on the touch pad)?
bool sticksUsed();
// Place the pointer and its primary button from the game (the aim stick).
// Going from up to down counts as a click; click() adds one more.
void setPointer(float x, float y, bool down);
void click();

} // namespace Input
