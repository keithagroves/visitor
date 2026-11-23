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
    // The stock tank. Measured with the lander's own physics (a native
    // harness, straight-up burns from the home surface): ~83 to climb to the
    // moon's distance, ~89 to escape the home world, then ~40 more to land in
    // another planet's gravity well. 160 is about 1.7x an ideal moon trip —
    // the moon with room to spare, not another planet in one hop (though the
    // moon, where the tank refills, is a stepping stone for a good pilot).
    // The fuel-tank upgrades multiply it (Trade::fuelScale). It was 250,
    // which reached anywhere.
    float maxFuel = 160.0f;
    // Fuel per frame at full thrust. 0.05 gives ~83 s of burn on a full tank:
    // enough to climb out of the home atmosphere (which alone took more than a
    // whole tank at the old 0.2), cross to a neighboring world, and land.
    float fuelBurn = 0.05f;
    float refuelRate = 0.25f;      // fuel per frame regained while landed (0 = off)
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
    // Pixels per world unit when on or near the ground. 1.8 puts the astronaut
    // (36 units tall) and the ship at about a twelfth of the screen height —
    // close enough to see who you are. (It was 0.2: a seven-pixel astronaut.)
    float cameraGroundZoom = 1.8f;
    float cameraFraming = 0.4f;
    // Speed look-ahead, seconds: the camera pulls back so that, at your
    // current speed, this much travel still fits in the frame. It is what
    // makes a close ground zoom flyable — zoomed in while you stand or walk,
    // wide while the ship is moving fast near the ground. 0 = off.
    float cameraSpeedLook = 2.2f;
    // On-foot movement. walkSpeed is the top running speed in world units per
    // frame (2.4 = about four body lengths a second; it was ~17, half a body
    // length every frame). jumpSpeed is the take-off speed: 3.0 clears a bit
    // over one body height on the home world and far more on a low-gravity
    // moon (it was 8: nine body heights at home).
    float walkSpeed = 2.4f;
    float jumpSpeed = 3.0f;
    // How far below screen center the ship sits when on the ground (fraction of
    // screen height): puts the horizon on the lower third so the sky gets the
    // frame. Eases back to centered as the ship climbs.
    float cameraHorizon = 0.14f;
    float horizonHaze = 0.7f;      // strength of the pale aerial haze band at the horizon
    float foreground = 1.0f;       // height of the dark near framing band (0 = off)
    // Edge vignette strength (1 = the full strip alphas in drawVignette, 0 =
    // off). The vignette pass existed before but was never flushed to the
    // screen; it is on at a gentle strength now that it actually draws.
    float vignette = 0.35f;
    // Drifting dust motes over the whole frame (0 = off, 1 = original
    // strength). Off by default: as small pale squares drawn in front of the
    // land they read as the starfield showing through the mountains.
    float motes = 0.0f;
    // The front strip of ground is solid where it passes in front of a hole —
    // except around a player who is down one, where it thins to this (0 =
    // vanishes there, 1 = stays solid and hides them).
    float frontVeil = 0.45f;
    // A hole is a darker shade of the ground it was cut in, darker the deeper
    // it goes (see holeShade in the body shader): it levels off at holeFloor x
    // the ground once it is holeDepth deep, so it never goes black.
    float holeFloor = 0.34f;
    float holeDepth = 150.0f;
    // How much ore is buried in the ground (deposits.cpp): scales every grid
    // square's chance of holding a vein. 1 = a find every ten seconds or so
    // of tunnelling; 0 = none.
    float oreRichness = 1.0f;
    // The terrain cutter, held on a clump of ore, burns it away (nothing is
    // collected): this many times longer than the mining laser takes to mine
    // it. 3 = about four seconds for gold.
    float oreBurn = 3.0f;
    // Water. waterLevel raises (+) or lowers (-) every sea by this many world
    // units, for looking at a world wetter or drier (what was placed to suit
    // the generated level — mesas, plants, the drop-in point — stays put).
    // waterTint is how strongly whatever is under water takes its color
    // (0 = clear as air, 1 = hidden).
    float waterLevel = 0.0f;
    float waterTint = 0.42f;
    // Sun-angle override for tuning time-of-day looks: -5 = auto day cycle;
    // 0..360 pins the sun's orbit angle (270 = noon over the spawn point,
    // ~350 = sunset there).
    float sunOverride = -5.0f;
};

inline Debug gDebug;
