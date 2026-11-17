#pragma once

#include <string>
#include <vector>
#include "planet.h"

// Procedural solar systems. One seed deterministically produces a star, four
// to six planets on Kepler-consistent circular orbits around it, and a handful
// of moons — each body with its own archetype (palette, sky, clouds, terrain
// shape, gravity) and a generated name. The home world is always a temperate
// planet with the original tuning (radius, gravity, look), so the feel of the
// starting planet is stable while everything around it varies.
struct StarSystem {
    unsigned int seed = 0;
    std::string starName;
    int sunIdx = 0;    // always 0: parents precede their children in the vector
    int homeIdx = 1;
};

// Fill `planets` (cleared first) with a generated system. Bodies are placed at
// their starting orbit positions; fields stay lazy (see Planet::ensureField).
StarSystem generateSystem(unsigned int seed, std::vector<Planet>& planets);

// Recompute every orbit rate from its parent's current GM. Call after the
// global gravity knob changes so planets keep sharing orbits with the ships
// coasting beside them.
void updateKeplerSpeeds(std::vector<Planet>& planets);
