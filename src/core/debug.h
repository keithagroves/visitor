#pragma once

// Live, in-game-tunable knobs — the C++ twin of cow-abductor's DEBUG object.
// The HTML debug panel (shell.html) owns the UI + localStorage persistence and
// writes into window.DBG; main.cpp's syncDebugFromJS() copies those values into
// this struct each frame, so gameplay code can just read gDebug.* directly.
struct Debug {
    // 0.15 (the original's tuning) gives thrust-to-weight ~1.25 at the home
    // planet's surface (g = density * baseRadius / 8000 ≈ 0.1) — enough to
    // climb to orbit. Below ~0.1 the ship can never leave the ground.
    float thrust = 0.15f;
    float maxFuel = 250.0f;
    float beamRange = 300.0f;
    float beamWidth = 20.0f;
    float planetDensity = 0.025f;  // applied live via Planet::setDensity
    float atmosphereScale = 0.4f;  // atmosphere top = baseRadius * (1 + scale)
    float atmosphereDrag = 0.015f;
    // Atmosphere scattering (see the atmosphere shader in main.cpp).
    float atmRayleighH = 1.5f;     // surface-dense blue falloff (low = thicker overhead sky)
    float atmRimH = 7.5f;          // rim-dense blue falloff (blue toward space)
    float atmMieH = 2.0f;          // haze falloff (higher = hugs the surface)
    float atmSunIntensity = 44.0f; // overall sky brightness
    float atmExtinction = 4.5f;    // sun-path extinction (sunset reddening)
    // Clouds (see the cloud shader in main.cpp).
    float cloudHeight = 0.8f;      // how far up the atmosphere the cloud band reaches (0..1)
    float cloudCoverage = 1.0f;    // cloud density / coverage (0 = clear, 1 = overcast)
    float cloudFalloff = 0.65f;    // how strongly coverage thins toward both band edges (0 = none, 1 = clouds only mid-band)
    float spaceHaze = 1.0f;        // max strength of the sky/cloud fill over the planet disk from space (1 = solid sky circle, 0 = planet stays bare)
    // Day/night terminator on the planet body (see the terminator shader in main.cpp).
    float nightDarkness = 0.16f;   // brightness of the deepest night side (0 = pitch black, 1 = no darkening)
    float nightSoftness = 0.30f;   // half-width of the terminator band in sunDot units (higher = softer, more gradual)
    float cameraGroundZoom = 0.2f;
    float cameraFraming = 0.4f;
    // Sun-angle override for tuning time-of-day looks: -5 = auto day cycle;
    // 0..360 pins the sun's orbit angle (270 = noon over the spawn point,
    // ~350 = sunset there).
    float sunOverride = -5.0f;
};

inline Debug gDebug;
