#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include "../math/vecmath.h"
#include "../math/noise.h"
#include "../graphics/renderer2d.h"
#include "../core/debug.h"
#include "terrain_field.h"
#include "carve_map.h"

// Per-world visual identity: everything that makes one generated planet look
// different from the next while the art-style RULES stay fixed (five flat
// terrain tones, cel light steps, one flat color per ridge layer, soft computed
// sky — see docs/art-style.md). Filled in by the system generator
// (system_gen.cpp) from the planet's archetype; the defaults are the original
// home-world look, so a default-constructed Planet renders exactly as before.
struct PlanetLook {
    // Five flat terrain tones by altitude: basin, mid, high, crest, cap.
    float tones[5][3] = {
        {0.290f, 0.271f, 0.251f}, {0.431f, 0.412f, 0.392f}, {0.561f, 0.533f, 0.494f},
        {0.690f, 0.659f, 0.604f}, {0.930f, 0.950f, 0.980f}};
    // Sky: Rayleigh-style scatter color (what the day sky looks like; the
    // sunset hue falls out of it via extinction) + haze (Mie) scatter color.
    float skyBeta[3] = {0.0058f, 0.0135f, 0.0331f};
    float mieBeta[3] = {0.0080f, 0.0080f, 0.0080f};
    float skyGain = 1.0f;       // multiplier on the global sky brightness
    float atmoScale = 1.0f;     // multiplier on gDebug.atmosphereScale (shell thickness)
    float dragScale = 1.0f;     // multiplier on gDebug.atmosphereDrag (air density)
    // Pale aerial haze: the horizon glow color, and what distant ridge layers
    // dissolve into.
    float haze[3] = {168.0f / 255.0f, 182.0f / 255.0f, 208.0f / 255.0f};
    // Clouds: coverage multiplier (0 = none) and lit / shaded colors.
    float cloudCover = 1.0f;
    float cloudLit[3] = {1.0f, 0.98f, 0.94f};
    float cloudDark[3] = {0.45f, 0.50f, 0.60f};
    // Background ridge tints, farthest and nearest layer (lerped between).
    float ridgeFar[3] = {0.66f, 0.66f, 0.78f};
    float ridgeNear[3] = {0.39f, 0.42f, 0.44f};
    // Near framing plane (the dark foreground band) + ground clutter.
    float fgTint[3] = {0.17f, 0.18f, 0.22f};
    float frondFrac = 0.5f;     // share of foreground props that are fronds (rest: spires)
    float propDensity = 1.0f;   // 0 = bare ground (no tufts / props)
    // One signature color: map discs and how the world looks hanging in
    // another planet's sky.
    float sig[3] = {0.36f, 0.58f, 0.95f};
    // Things to harvest on the ground (see Planet::Harvestable): the plant
    // color, the crystal color, and how thickly each grows (0 = none).
    float flora[3] = {0.40f, 0.55f, 0.32f};
    float mineral[3] = {0.55f, 0.85f, 0.92f};
    float floraDensity = 1.0f;
    float mineralDensity = 0.35f;
    // Open water (Planet::sea): the tone of the shallows and of deep water,
    // before the hour's light. Flat fields, like the ground's.
    float water[3] = {0.27f, 0.52f, 0.60f};
    float waterDeep[3] = {0.13f, 0.31f, 0.44f};
};

// Shape knobs for the procedural silhouette (see Planet::generate).
struct TerrainStyle {
    int rangesMin = 1, rangesMax = 2;  // mountain-range clusters
    float rangeBoost = 1.0f;           // scales how tall the ranges stand
    float rolling = 1.0f;              // rolling-hill frequency scale
    int craters = 0;                   // impact bowls (airless bodies)
    // Mesas: flat-topped raised areas at the scale of a person (a couple of
    // hundred units tall), most with a natural cave cut into them. Average
    // count per 9000 units of ground; 0 = none.
    float mesaDensity = 1.0f;
    // Keep this stretch free of mesas (degrees; half-width 0 = no such
    // stretch): the home world's drop-in point, where the ship and the herd
    // start.
    float keepClearAngle = 0.0f, keepClearHalf = 0.0f;
    // Water. A world of this kind has open water with probability seaChance,
    // and then this share of its ground lies under it (drawn from seaMin ..
    // seaMax): the sea level is set where that much of the surface is below
    // it, so the low ground fills into ponds, lakes and seas. 0 = always dry.
    float seaChance = 0.0f, seaMin = 0.0f, seaMax = 0.0f;
};

// A procedurally generated planet body, ported from cow-abductor's planet.js.
// Owns a star-shaped terrain silhouette (radius varies with angle via Perlin
// noise + a couple of "mountain range" clusters), a 1/r^2 gravity field, and a
// soft atmosphere shell. Distances are in the same large world units as the
// original game (a starter planet is ~32000 units across).
//
// Planets live in a solar system (std::vector<Planet> in main.cpp): each can
// orbit a parent body kinematically, and the heavy destructible TerrainField is
// LAZY — only built (ensureField) for the planet the player is near, released
// when they leave, with dig operations recorded and replayed so craters
// persist across unload/reload.
class Planet {
public:
    // Tuning carried over from DEBUG in the original game.
    static constexpr float GRAVITY_REFERENCE_RADIUS = 8000.0f;

    struct LandPoint {
        float angle;  // degrees
        float r;      // radius from center
    };

    // Set atmosphere / isSun / look BEFORE calling (ridges and clutter read them).
    void generate(Vec2 center, float baseRadius, float noiseIntensity,
                  int numPoints, float density, unsigned int seed,
                  const TerrainStyle& style = TerrainStyle());

    // GM term; surface gravity scales with size (see planet.js setDensity).
    void setDensity(float density);

    // Apply the global gravity knob (gDebug.planetDensity): every body keeps
    // its own relative density (densityRel), stars use an explicit GM (gmRef,
    // quoted at the reference density) — both scale with the knob together, so
    // Kepler-consistent orbits stay consistent.
    static constexpr float REFERENCE_DENSITY = 0.025f;
    void applyGlobalDensity(float globalDensity);

    // Outermost surface radius at a given angle (degrees). Reads the fast CPU
    // surfaceLUT — rebuilt from the destructible field when one is live, or
    // from the analytic silhouette otherwise — so collision, landing, and the
    // shader silhouette all reflect craters dug from the surface.
    float surfaceRadius(float angleDeg) const;
    float maxSurfaceRadius() const;

    // ---- The rock itself -------------------------------------------------
    // Signed density at a world point: > 0 solid, < 0 open, ~distance to the
    // surface near it. This is the terrain (the live field, or the silhouette
    // when no field is loaded) with everything that has been dug removed — the
    // one definition of "is there rock here" that the body shader, the
    // astronaut's collision and the cutter all share.
    float rockAt(Vec2 world) const;
    // Unit normal of the rock surface near a point, pointing out into the open.
    Vec2 normalAt(Vec2 world) const;
    // The terrain alone, before anything was dug (same sign convention).
    float terrainAt(Vec2 world) const;
    // The flat ground tone the body shader gives a world point (its altitude
    // band, with the same noise-jittered band edges), before any light: the
    // CPU twin of the banding in kBodyFrag. What a cave's wall behind a
    // formation is a shade of.
    void groundTone(Vec2 world, float out[3]) const;

    // Cut a circular bite out of the ground (see CarveMap). Returns the part
    // of the carve window that changed, for a partial texture upload; empty if
    // nothing was cut. Every bite is remembered, so holes persist.
    CarveMap::Rect dig(Vec2 world, float radius);
    // Keep the fine carve window around a world point: (re)builds it when the
    // point is outside or near its edge. Returns true when the window was
    // rebuilt and its whole texture needs re-uploading.
    bool focusCarve(Vec2 world);
    bool hasDigs() const { return !digOps_.empty(); }

    // ---- Water ---------------------------------------------------------------
    // One level round the whole world (seaLevel, above baseRadius): wherever
    // the ground dips below it there is a pond, a lake or a sea. Water also
    // stands in whatever has been dug that connects to one, up to the same
    // level (CarveMap works out what has flooded); a hole that touches no
    // water stays dry however deep it goes.
    bool  sea = false;       // this world has open water
    float seaLevel = 0.0f;   // its surface, above baseRadius
    float seaRadius() const { return sea ? baseRadius + seaLevel + gDebug.waterLevel : 0.0f; }
    // The ground as you see it from above: the terrain, or the water over it.
    // What the ship comes down on, and what shadows fall on.
    float visibleRadius(float angleDeg) const;
    bool overWater(float angleDeg) const { return sea && seaRadius() > surfaceRadius(angleDeg); }
    // How far under the surface a world point is, if it is in water (in the
    // sea, or in flooded dug space); 0 if it is not.
    float waterDepth(Vec2 world) const;

    // ---- Lazy destructible field ------------------------------------------
    // Build the density grid (seeded from the silhouette, past digs replayed).
    // Call when the player approaches; release when they leave. surfaceRadius()
    // works either way.
    void ensureField();
    void releaseField();
    bool fieldReady() const { return fieldReady_; }

    // ---- Orbit (kinematic circular, ported from planet.js) -----------------
    // parentIdx indexes the system's planet vector; -1 = pinned in place.
    // updateOrbit recomputes center from the parent and shifts the live field
    // along; orbitVel is the per-frame-unit world motion (inherit on liftoff).
    void setOrbit(int parentIdx, float radius, float speedDegPerFrame, float startAngleDeg);
    void updateOrbit(const std::vector<Planet>& planets, float timeScale);
    // Circular-orbit rate around a parent of the given GM (Kepler: w^2 r^3 = GM),
    // so a ship coasting alongside the planet shares its orbit instead of being
    // left behind.
    void setKeplerSpeed(float parentGravity);

    // 0..1 atmospheric density at a world point (1 at surface, 0 at the top).
    // Always 0 on airless bodies (atmosphere == false).
    float atmosphericDensity(float x, float y) const;
    float atmosphereThickness() const {
        return atmosphere ? baseRadius * gDebug.atmosphereScale * look.atmoScale : 0.0f;
    }
    float atmosphereOuterRadius() const { return baseRadius + atmosphereThickness(); }

    Vec2 center;
    float baseRadius = 0;
    float noiseIntensity = 0;
    float gravity = 0;       // GM term used by every gravity consumer
    float density = 0;
    float densityRel = 1.0f; // this body's density relative to the global knob
    float gmRef = 0;         // > 0: explicit GM at REFERENCE_DENSITY (stars)

    unsigned int seed = 0;   // terrain seed (also keys per-planet decoration hashes)

    // Identity (system generator).
    std::string name;        // generated proper name
    std::string kind;        // archetype label ("Ice world", "Barren moon", ...)
    int archetype = 0;       // 0 temperate, 1 desert, 2 ice, 3 volcanic, 4 jungle,
                             // 5 exotic, 6 barren — also picks which crystal grows here
    PlanetLook look;
    bool discovered = false; // the player has been close enough to chart it
    bool visited = false;    // the player has landed on it

    bool isSun = false;      // drawn as a glowing star; excluded from body/sky passes
    bool atmosphere = true;  // false = airless body (no sky, no clouds, no drag)

    int orbitParent = -1;    // index into the system vector; -1 = pinned
    float orbitRadius = 0;
    float orbitSpeed = 0;    // degrees per frame-unit
    float orbitAngle = 0;    // degrees
    Vec2 orbitVel;           // world velocity, units per frame-unit (incl. the parent's)
    Vec2 orbitStep;          // exact displacement of the center in the last updateOrbit
    bool orbitPinned = false;// frozen on its orbit (debug sun-angle override)

    std::vector<LandPoint> landscape;  // closed loop (last == first); seeds the field
    TerrainField field;                // coarse crust (SDF density grid), lazy
    CarveMap carve;                    // what has been dug, at person scale (window around the player)

    // Distant mountain silhouettes drawn behind the terrain (ported from the
    // original's generateBackgroundRidge). Each layer follows a fraction of the
    // player's surface angle so the horizon slides with depth as you fly.
    struct Ridge {
        std::vector<LandPoint> points; // closed loop (last == first)
        float follow;                  // fraction of the view angle the layer tracks
        float lift;                    // radial offset so ridges sit above valleys
        float alpha;                   // layer opacity (0..1)
        float skyMix;                  // how far the tint fades toward sky haze
        float tintR, tintG, tintB;     // base hill color before haze/shading
        float topR;                    // max crest radius incl. lift (gradient anchor)
    };
    std::vector<Ridge> ridges;         // far-to-near order

    // Harvestables: plants and minerals standing on the strip of ground around
    // the walking line — some a little behind it (toward the horizon edge),
    // some a little in front (toward the viewer, drawn over whoever walks
    // past). `depth` is that front/back position; nearfield.cpp turns it into
    // a place on screen, a size and a parallax, which is what gives the ground
    // its depth. Placed once at generate(); the mining laser harvests them and
    // they grow back. (The dark framing band along the bottom of the screen
    // and its props are separate: main's drawForeground.)
    struct Harvestable {
        enum Kind : unsigned char { Grass = 0, Bulb = 1, Crystal = 2, Rock = 3 };
        float angle;       // degrees around the planet
        float depth;       // world units toward the viewer (+) / away (-) from the walking line
        float size;        // height, world units
        float phase;       // sway / shape variation
        Kind  kind;
        float hp;          // beam-seconds left before it comes apart
        float grow;        // 0..1 — below 1 while growing back
        float regrowIn;    // seconds until a harvested one starts growing back (0 = alive)
    };
    std::vector<Harvestable> harvest;   // sorted by angle (NearField searches it); any spacing

    // A flat-topped raised area (see TerrainStyle::mesaDensity). Distances are
    // world units of ground measured at baseRadius.
    struct Mesa {
        float angle;       // center, degrees
        float half;        // center to foot
        float edge;        // width of each flank (foot to the top's rim)
        float height;      // top above the ground around it
    };
    std::vector<Mesa> mesas;

    // Something growing in (or lying at the mouth of) a natural cave:
    // formations and things to mine. Placed by buildCaves; drawn, picked and
    // mined through CaveField. Purely scenery as far as collision goes — like
    // the plants, they stand behind or in front of the plane you walk in.
    struct CaveProp {
        enum Kind : unsigned char {
            Stalactite = 0,    // hangs from a ceiling            -> calcite
            Stalagmite = 1,    // stands on a floor               -> calcite
            Column = 2,        // floor to ceiling                -> calcite
            Crystal = 3,       // a glowing cluster, any surface  -> the world's crystal
            Glowcap = 4,       // luminous mushrooms on a floor   -> glowcap (grows back)
            GoldVein = 5,      // flecks in the rock, deep down   -> gold
            Boulder = 6,       // rubble round a cave mouth       -> ferrite
        };
        Vec2  local;       // where it is rooted, planet-local (the physical point)
        float dir;         // degrees: the way it grows out of the rock
        float size;        // length, world units
        float phase;       // shape variation / animation offset
        Kind  kind;
        bool  front;       // drawn over the walker instead of behind
        bool  inside;      // inside a cave (hidden from the surface) vs out at a mouth
        float hp;          // 1 = untouched .. 0 = mined out
        float goneFor;     // > 0: mined; seconds until it grows back (kinds that do)
    };
    std::vector<CaveProp> caveProps;

    // Where a natural cave opens through the ground (degrees, center and
    // half-width): the body shader gives these mouths an uneven, natural rim.
    struct CaveMouth { float angle, half; };
    std::vector<CaveMouth> caveMouths;

    // A clump of ore buried in the rock (deposits.cpp). Clumps are seeded on a
    // grid from the planet's seed and worked out when needed, so nothing is
    // stored for one until it is found: digging up to it (or a cave wall
    // running past it) reveals it, and the mining laser takes it out a piece
    // at a time — or the terrain cutter, held on it, burns it away.
    struct Ore {
        enum Kind : unsigned char { Ferrite = 0, Crystal = 1, Gold = 2 };
        Vec2  local;       // center, planet-local (where it physically is)
        float size;        // radius, world units
        float phase;       // shape variation / glint timing
        Kind  kind;
        bool  mined;       // gone (for good): mined out, or burnt away
        float hp;          // 1 = untouched .. 0 = mined out; what is taken stays taken
        float heat;        // the cutter's: 0 = cold .. 1 = burnt away; cools when left alone
        float found;       // seconds since it was revealed (the reveal glint)
    };
    std::vector<Ore> ores;                             // the clumps found so far
    // Every grid clump looked at so far, by id: its index in `ores`, or -1
    // when a cave had already hollowed it out (there is no clump).
    std::unordered_map<unsigned long long, int> oreSeen;

    // ---- The locals ---------------------------------------------------------
    // Every world has people of its own — one kind per world, its look drawn
    // from the world's seed — who keep a few trading posts on dry, level
    // ground: a hut, a stall with the trader behind its counter, a lantern
    // mast, and a couple of locals wandering about. Placed at generate();
    // simulated, drawn and picked through Outposts (outposts.cpp). What they
    // pay and what they sell is in trade.cpp.
    struct Folk {
        float body[3] = {0.45f, 0.62f, 0.55f};    // skin, the lit tone
        float accent[3] = {0.93f, 0.88f, 0.74f};  // what they wear: their world's crystal color
        float height = 26.0f;    // an average one, world units (the astronaut is 36)
        float girth = 0.7f;      // width / height
        int   eyes = 2;          // 1..3
        int   head = 0;          // 0 one-piece blob, 1 a head on a neck, 2 eyes on stalks
        int   antennae = 0;      // 0..2
        bool  helmet = false;    // airless world: a glass bubble over the head
    };
    struct Alien {
        float along = 0;         // where it stands: ground units from the stall (+ = the way angles grow)
        float lo = 0, hi = 0;    // the stretch it wanders (lo == hi: it stays put)
        float goal = 0;          // where it is walking to
        float pause = 0;         // seconds it stands before picking somewhere new
        float size = 1;          // height multiplier (they are not all the same)
        float phase = 0;         // animation offset
        float step = 0;          // walk cycle
        float moving = 0;        // 0..1 eased: walking
        float wave = 0;          // 0..1 eased: an arm up, waving
        float noticed = 0;       // 0..1 eased: looking at the visitor
        int   facing = 1;        // +1 = the way angles grow (screen right)
        bool  trader = false;
    };
    struct Outpost {
        float angle = 0;         // where the stall stands, degrees
        unsigned int seed = 0;   // its prices, its trader's name
        unsigned int rng = 1;    // the wanderers' dice
        std::string keeper;      // the trader's name
        std::vector<Alien> folk; // [0] is the trader
        // What is left of the goods it sells, per slot (see Trade); -1 = not
        // looked at yet.
        int   stock[2] = {-1, -1};
        float hail = 0;          // seconds of waving left (the trader has just seen you)
        bool  near = false;      // the visitor is close by (edge detector for the wave)
        bool  met = false;       // the player has traded here
    };
    Folk folk;
    std::vector<Outpost> outposts;

private:
    // The original star-shaped silhouette, interpolated from `landscape`, plus
    // the mesas. Used to seed the field and as the LUT source while no field
    // is live.
    float seedRadius(float angleDeg) const;
    float seedRadiusBase(float angleDeg) const;    // without the mesas
    // How far the mesas raise the ground at an angle (0 away from them).
    float mesaOffset(float angleDeg) const;
    // Scatter the plants and minerals (`harvest`) the way they would grow:
    // in patches, where the ground suits them. See harvest_gen.cpp.
    void growHarvest();
    // Decide what this world's people look like (`folk`) and where their
    // trading posts stand (`outposts`): dry, level ground clear of the mesas,
    // one of them a short walk from the home world's drop-in point. Clears
    // the plants and stones off the ground they stand on. See outposts.cpp.
    void foundOutposts(const TerrainStyle& style);
    // Cut the natural caves into and under the mesas (as bites at the front of
    // digOps_, so they live in the same fine layer as anything the player
    // digs) and furnish them (caveProps). Needs the field: cave floors are
    // laid on the ground it actually has. Once. See cave_gen.cpp.
    void buildCaves();
    void rebuildSurfaceLUT();
public:
    // Entries in the outline table. Fine enough that straight runs between
    // entries stay within a unit or two of the grid's own 0 line even where
    // a steep slope turns a corner (about 25 units of ground per entry on
    // the home world; at 2048 the table cut those corners by up to ~15
    // units, which showed as gaps between the ground and everything placed
    // or clipped by the table).
    static constexpr int LUT_N = 8192;
private:
    std::vector<float> surfaceLUT;     // angle (uniform) -> outer surface radius

    // terrainAt, planet-local, for the carve layer to tell sky from rock.
    CarveMap::Terrain terrainLocal() const;

    bool fieldReady_ = false;
    bool cavesBuilt_ = false;
    int naturalOps_ = 0;                         // digOps_[0..naturalOps_) are the caves
    std::vector<CarveMap::Op> digOps_;           // every bite ever cut, planet-local
};
