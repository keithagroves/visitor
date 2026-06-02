#pragma once

// Live, in-game-tunable knobs — the C++ twin of cow-abductor's DEBUG object.
// The HTML debug panel (shell.html) owns the UI + localStorage persistence and
// writes into window.DBG; main.cpp's syncDebugFromJS() copies those values into
// this struct each frame, so gameplay code can just read gDebug.* directly.
struct Debug {
    float thrust = 0.15f;
    float maxFuel = 250.0f;
    float beamRange = 300.0f;
    float beamWidth = 20.0f;
    float planetDensity = 0.03f;   // applied live via Planet::setDensity
    float atmosphereScale = 1.0f;  // atmosphere top = baseRadius * (1 + scale)
    float atmosphereDrag = 0.015f;
    // Atmosphere scattering (see the atmosphere shader in main.cpp).
    float atmRayleighH = 1.0f;     // blue air density falloff with altitude (low = thick sky)
    float atmMieH = 11.0f;         // haze falloff (higher = hugs the surface)
    float atmSunIntensity = 20.0f; // overall sky brightness
    float atmExtinction = 8.0f;    // sun-path extinction (sunset reddening)
    float cameraGroundZoom = 2.0f;
    float cameraFraming = 0.4f;
};

inline Debug gDebug;
