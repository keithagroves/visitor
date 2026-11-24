#pragma once
#include <cmath>

// A player's colour: one hue for the ship's lights and the suit's trim and
// lights, so players are told apart at a glance. Online, the server hands
// everyone in a system a different hue (server.mjs / worker: pickHue).
// Beams keep their tool's colours (laser orange, cutter teal, tractor teal).
struct Livery {
    float hue = 171.0f;   // the original teal
    float suit[3];        // the suit's cloth: a pale tint (the helmet stays white)
    float suitShade[3];   // ...its shaded side (far limbs, under the hips)
    float dye[3];         // the suit's trim: a strong tone on the pale cloth
    float wing[3];        // the ship's wings: dark metal with the hue in it
    float crown[3];       // the ship's lit bevel down the hull
    float glow[3];        // a light: the ship's core, the chest light, the visor glint
    float bright[3];      // a light's lit rim (the cockpit)
    float hot[3];         // a light's white-hot centre

    explicit Livery(float h = 171.0f) { set(h); }

    void set(float h) {
        hue = std::fmod(std::fmod(h, 360.0f) + 360.0f, 360.0f);
        // Yellow to lime is pale against the white suit: darker there.
        float d = std::fabs(hue - 65.0f);
        float yellow = d < 45.0f ? 1.0f - d / 45.0f : 0.0f;
        hsl(hue, 0.55f, 0.80f - 0.04f * yellow, suit);
        hsl(hue, 0.38f, 0.64f - 0.04f * yellow, suitShade);
        hsl(hue, 0.80f, 0.48f - 0.08f * yellow, dye);
        hsl(hue, 0.55f, 0.17f, wing);
        hsl(hue, 0.45f, 0.30f, crown);
        hsl(hue, 1.0f, 0.65f, glow);
        hsl(hue, 1.0f, 0.77f, bright);
        hsl(hue, 1.0f, 0.90f, hot);
    }

    static void hsl(float h, float s, float l, float out[3]) {
        float c = (1.0f - std::fabs(2.0f * l - 1.0f)) * s;
        float hp = h / 60.0f;
        float x = c * (1.0f - std::fabs(std::fmod(hp, 2.0f) - 1.0f));
        float r = 0, g = 0, b = 0;
        if (hp < 1)      { r = c; g = x; }
        else if (hp < 2) { r = x; g = c; }
        else if (hp < 3) { g = c; b = x; }
        else if (hp < 4) { g = x; b = c; }
        else if (hp < 5) { r = x; b = c; }
        else             { r = c; b = x; }
        float m = l - c * 0.5f;
        out[0] = r + m; out[1] = g + m; out[2] = b + m;
    }
};
