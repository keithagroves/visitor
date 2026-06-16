#include "lander.h"
#include <cmath>

void Lander::reset() {
    vel = Vec2(0, 0);
    pos = Vec2(0, 0);
    rotation = targetRotation = -90.0f;
    fuel = gDebug.maxFuel;
    thrusting = 0.0f;
    thrustLevel = 0.0f;
    active = true;
    crashed = false;
    landed = false;
    altitude = 0.0f;
    anim = 0.0f;
}

void Lander::spawnOnPlanet(const Planet& planet) {
    // Drop in just above the surface; orient the ship's "top" radially outward
    // so W thrusts away from the planet (rotation = angle + 90, per lander.js).
    float angle = -90.0f; // start over the "north" pole-ish point
    float surf = planet.surfaceRadius(angle);
    float r = surf + 2000.0f;  // drop in high enough to fly the descent
    pos = Vec2(planet.center.x + r * cosd(angle),
               planet.center.y + r * sind(angle));
    vel = Vec2(0, 0);
    rotation = targetRotation = angle + 90.0f;
    active = true;
    crashed = false;
    landed = false;
}

void Lander::rotate(float direction) {
    targetRotation += direction * 15.0f;
}

void Lander::setThrust(float power) {
    if (fuel <= 0) power = 0;
    thrusting = power;
}

Vec2 Lander::gravityAt(const Planet& planet, Vec2 p) const {
    float dx = planet.center.x - p.x;
    float dy = planet.center.y - p.y;
    float distSq = dx * dx + dy * dy;
    if (distSq < 1.0f) distSq = 1.0f;
    float distance = sqrtf(distSq);
    float force = planet.gravity / distSq;
    return Vec2((dx / distance) * force, (dy / distance) * force);
}

Vec2 Lander::gravityAt(const std::vector<Planet>& planets, Vec2 p) const {
    Vec2 g(0, 0);
    for (const Planet& pl : planets) g += gravityAt(pl, p);
    return g;
}

namespace {
// Strongest atmospheric density over all bodies at a point. Atmospheres never
// overlap in practice, so this is "the one you're inside" (0 in open space).
float systemDensity(const std::vector<Planet>& planets, Vec2 p) {
    float best = 0.0f;
    for (const Planet& pl : planets) {
        float d = pl.atmosphericDensity(p.x, p.y);
        if (d > best) best = d;
    }
    return best > 1.0f ? 1.0f : best;
}
}

void Lander::update(const std::vector<Planet>& planets, float timeScale) {
    if (!active) return;

    anim += timeScale;

    if (thrusting > 0 && fuel <= 0) thrusting = 0;

    // Smooth rotation toward the target heading.
    rotation += (targetRotation - rotation) * 0.3f * timeScale;

    // Substep count from current atmospheric density: the multiplicative drag
    // form collapses to zero when k*dt approaches 1, so split the step to keep
    // each slice in the stable regime.
    float densityNow = systemDensity(planets, pos);
    float kdt = gDebug.atmosphereDrag * densityNow * timeScale;
    int substeps = (int)std::ceil(kdt / SUBSTEP_MAX_KDT);
    if (substeps < 1) substeps = 1;
    float dt = timeScale / substeps;

    // Fuel burn is per-frame, independent of substep slicing.
    if (thrusting > 0 && fuel > 0) {
        fuel -= 0.2f * thrusting * timeScale;
    }

    for (int i = 0; i < substeps; i++) {
        // Gravity: sum over every body in the system.
        vel += gravityAt(planets, pos) * dt;

        // Thrust along the ship's local "up": (sin(rot), -cos(rot)).
        if (thrusting > 0 && fuel > 0) {
            float force = gDebug.thrust * thrusting * dt;
            vel.x += sind(rotation) * force;
            vel.y -= cosd(rotation) * force;
        }

        // Atmospheric drag toward the (static) planet frame.
        float density = systemDensity(planets, pos);
        if (density > 0) {
            float factor = 1.0f - gDebug.atmosphereDrag * density * dt;
            if (factor < 0) factor = 0;
            vel *= factor;
        }

        // Velocity clamp.
        float speedSq = vel.lengthSq();
        if (speedSq > TOP_SPEED * TOP_SPEED) {
            float speed = sqrtf(speedSq);
            vel *= TOP_SPEED / speed;
        }

        pos += vel * dt;
    }

    if (fuel < 0) fuel = 0;
    thrustLevel += (thrusting - thrustLevel) * 0.4f;
}

bool Lander::isUpright(const Planet& planet) const {
    float tx = planet.center.x - pos.x;
    float ty = planet.center.y - pos.y;
    float d = sqrtf(tx * tx + ty * ty);
    if (d < 1e-6f) return false;
    tx /= d; ty /= d;
    // Local +Y is the ship's "legs" direction after rotation.
    float bottomX = -sind(rotation);
    float bottomY = cosd(rotation);
    float alignment = bottomX * tx + bottomY * ty;
    return alignment >= cosd(LANDING_MAX_TILT);
}

void Lander::land(const Planet& planet) {
    active = false;
    crashed = false;
    landed = true;
    thrusting = 0.0f;
    thrustLevel = 0.0f;
    vel = Vec2(0, 0);
    // Rest the ship with its base sunk below the terrain silhouette line, so
    // the ground reads as extending toward the viewer (the ~40 degree view
    // angle of the ground-ellipse language). The hull base sits 22*SCALE below
    // center; it should land on the blob shadow's contact point, which sits
    // radius*1.5*0.64 (the shadow drop in drawBlobShadow) below the line.
    float dx = pos.x - planet.center.x;
    float dy = pos.y - planet.center.y;
    float d = sqrtf(dx * dx + dy * dy);
    if (d > 1e-6f) {
        float angle = atan2d(dy, dx);
        float rest = planet.surfaceRadius(angle) + 22.0f * SCALE - radius * 1.5f * 0.64f;
        pos = Vec2(planet.center.x + (dx / d) * rest,
                   planet.center.y + (dy / d) * rest);
    }
    landingOffset = Vec2(pos.x - planet.center.x, pos.y - planet.center.y);
}

void Lander::crash() {
    active = false;
    crashed = true;
    landed = false;
    thrusting = 0.0f;
    thrustLevel = 0.0f;
}

void Lander::liftOff(const Planet& planet) {
    active = true;
    crashed = false;
    landed = false;
    // Nudge a little out the top of the ship so the next contact check doesn't
    // immediately re-land us.
    float upX = sind(rotation);
    float upY = -cosd(rotation);
    pos.x += upX * (radius + 5.0f);
    pos.y += upY * (radius + 5.0f);
    // Inherit the body's orbital motion so lifting off a moving moon doesn't
    // instantly smear you across its surface.
    vel = planet.orbitVel;
}

namespace {
// March along the beam direction until it dips below the terrain silhouette,
// so the cone visually stops at the ground. Capped at maxRange.
float beamLength(const Planet& planet, Vec2 origin, float dx, float dy, float maxRange) {
    const float step = 4.0f;
    for (float t = 0; t <= maxRange; t += step) {
        float px = origin.x + dx * t;
        float py = origin.y + dy * t;
        float ddx = px - planet.center.x;
        float ddy = py - planet.center.y;
        float d = sqrtf(ddx * ddx + ddy * ddy);
        if (d <= planet.surfaceRadius(atan2d(ddy, ddx))) return t;
    }
    return maxRange;
}
}

namespace {
unsigned int g_beamRng = 7919;
float beamRand(float a, float b) {
    g_beamRng = g_beamRng * 1664525u + 1013904223u;
    return a + ((g_beamRng >> 8) * (1.0f / 16777216.0f)) * (b - a);
}
}

void Lander::updateBeam(const Planet& planet) {
    // Advance + cull existing dust every frame so released motes finish their
    // rise even just after the beam cuts out.
    for (size_t i = 0; i < beamDust.size();) {
        beamDust[i].t += beamDust[i].speed;
        if (beamDust[i].t >= 1.0f) {
            beamDust[i] = beamDust.back();
            beamDust.pop_back();
        } else {
            i++;
        }
    }

    beamOn = false;
    if (!abducting || !active) return;

    // Beam fires straight down the ship's local axis (opposite of thrust).
    float nx = -sind(rotation);
    float ny = cosd(rotation);
    float len = beamLength(planet, pos, nx, ny, gDebug.beamRange);
    beamEnd = Vec2(pos.x + nx * len, pos.y + ny * len);

    float bw = gDebug.beamWidth;
    beamPerp = Vec2(-ny, nx);  // unit axis across the beam
    beamA = pos;
    beamB = Vec2(beamEnd.x + beamPerp.x * bw, beamEnd.y + beamPerp.y * bw);
    beamC = Vec2(beamEnd.x - beamPerp.x * bw, beamEnd.y - beamPerp.y * bw);
    beamOn = true;

    // Spawn a few dust motes at the ground end each frame.
    for (int i = 0; i < 3; i++) {
        beamDust.push_back({0.0f, beamRand(0.02f, 0.045f), beamRand(-0.3f, 0.3f)});
    }
}

void Lander::drawBeam(Renderer2D& r) {
    if (!beamOn) return;
    using Color = Renderer2D::Color;
    r.setBlend(Renderer2D::Blend::Additive);

    Vec2 p = beamPerp;
    float hw = gDebug.beamWidth;
    Color clear{0.43f, 0.90f, 0.78f, 0.0f};

    // --- Outer halo: a soft teal wedge fading to nothing at the edges. ---
    {
        Color apex{0.43f, 0.90f, 0.78f, 0.10f};
        Color core{0.43f, 0.90f, 0.78f, 0.16f};
        float widen = 1.9f;
        Vec2 lw(beamEnd.x + p.x * hw * widen, beamEnd.y + p.y * hw * widen);
        Vec2 rw(beamEnd.x - p.x * hw * widen, beamEnd.y - p.y * hw * widen);
        r.triangleV(pos, apex, lw, clear, beamEnd, core);
        r.triangleV(pos, apex, beamEnd, core, rw, clear);
    }

    // --- Core cone: bright down the centerline, transparent at the edges,
    // brightening from the ship toward the ground splat. ---
    {
        Color apex{0.70f, 1.0f, 0.86f, 0.30f};
        Color core{0.80f, 1.0f, 0.90f, 0.55f};
        r.triangleV(pos, apex, beamB, clear, beamEnd, core);
        r.triangleV(pos, apex, beamEnd, core, beamC, clear);
    }

    // --- Pulsing inner core spine. ---
    {
        float pulse = 0.6f + 0.4f * sind(anim * 360.0f);
        float innerHalf = hw * 0.35f;
        Color apex{0.90f, 1.0f, 0.96f, 0.0f};
        Color core{0.92f, 1.0f, 0.97f, 0.55f * pulse};
        Vec2 il(beamEnd.x + p.x * innerHalf, beamEnd.y + p.y * innerHalf);
        Vec2 ir(beamEnd.x - p.x * innerHalf, beamEnd.y - p.y * innerHalf);
        r.triangleV(pos, apex, il, clear, beamEnd, core);
        r.triangleV(pos, apex, beamEnd, core, ir, clear);
    }

    // --- Scrolling suction stripes traveling from the ground up to the ship. ---
    {
        float scroll = std::fmod(anim * 0.02f, 1.0f);
        const int stripes = 8;
        for (int i = 0; i < stripes; i++) {
            float phase = std::fmod((float)i / stripes + scroll, 1.0f); // 0 ship -> 1 end
            float cx = pos.x + (beamEnd.x - pos.x) * phase;
            float cy = pos.y + (beamEnd.y - pos.y) * phase;
            float w = hw * phase;
            float a = 0.78f * sind(phase * 180.0f);
            r.fill(0.59f, 1.0f, 0.86f, a);
            r.line(cx + p.x * w, cy + p.y * w, cx - p.x * w, cy - p.y * w, 2.0f);
        }
    }

    // --- Layered ground splat: stacked breathing oblongs at the impact. ---
    // Ground-plane ellipse language: ~64% height/width, matching the blob
    // shadows' ~40 degree view angle.
    {
        float breath = 0.9f + 0.1f * sind(anim * 240.0f);
        for (int i = 4; i >= 1; i--) {
            float rx = hw * (0.6f + i * 0.4f) * breath;
            r.fill(0.70f, 1.0f, 0.86f, 0.10f);
            r.ellipse(beamEnd.x, beamEnd.y, rx, rx * 0.64f, 18);
        }
    }

    // --- Dust motes riding the beam up to the ship. ---
    for (const Dust& d : beamDust) {
        float remaining = 1.0f - d.t;
        float cx = beamEnd.x + (pos.x - beamEnd.x) * d.t;
        float cy = beamEnd.y + (pos.y - beamEnd.y) * d.t;
        float lateral = d.off * hw * remaining;
        r.fill(0.86f, 1.0f, 0.92f, 0.86f * remaining);
        r.circle(cx + p.x * lateral, cy + p.y * lateral, 1.0f + 1.5f * remaining, 8);
    }

    r.setBlend(Renderer2D::Blend::Normal);
}

void Lander::predictTrajectory(const std::vector<Planet>& planets, std::vector<Vec2>& out) const {
    out.clear();
    const float dt = 1.0f;        // one frame-unit per substep (matches the sim)
    const int steps = 320;        // recorded points
    const int subStride = 14;     // ~4500 frame-units of horizon -> wraps an orbit

    Vec2 sp = pos, sv = vel;
    out.push_back(sp);   // anchor the line at the ship itself
    // Don't terminate on a body we start inside the bounding radius of (e.g.
    // just after lift-off); only stop once we've left it and re-entered.
    bool outside = true;
    for (const Planet& pl : planets) {
        if (dist(sp.x, sp.y, pl.center.x, pl.center.y) <= pl.baseRadius) outside = false;
    }

    for (int i = 0; i < steps; i++) {
        for (int s = 0; s < subStride; s++) {
            sv += gravityAt(planets, sp) * dt;
            float density = systemDensity(planets, sp);
            if (density > 0) {
                float f = 1.0f - gDebug.atmosphereDrag * density * dt;
                if (f < 0) f = 0;
                sv *= f;
            }
            float speedSq = sv.lengthSq();
            if (speedSq > TOP_SPEED * TOP_SPEED) sv *= TOP_SPEED / sqrtf(speedSq);
            sp += sv * dt;

            bool insideAny = false;
            for (const Planet& pl : planets) {
                float d = dist(sp.x, sp.y, pl.center.x, pl.center.y);
                // Beyond this radius the path can't possibly touch terrain, so
                // we skip the surfaceRadius() lookup. That keeps the long
                // horizon affordable.
                if (d <= pl.baseRadius + pl.noiseIntensity) {
                    float surf = pl.surfaceRadius(
                        atan2d(sp.y - pl.center.y, sp.x - pl.center.x));
                    if (outside && d <= surf) { out.push_back(sp); return; }
                }
                if (d <= pl.baseRadius) insideAny = true;
            }
            if (!insideAny) outside = true;
        }
        out.push_back(sp);
    }
}

void Lander::draw(Renderer2D& r) {
    r.pushMatrix();
    r.translate(pos.x, pos.y);
    r.scale(SCALE);
    r.rotate(rotation);

    // Thrust flame first so the hull sits on top of it (additive plasma cone).
    if (thrusting > 0 && active && fuel > 0) {
        r.setBlend(Renderer2D::Blend::Additive);
        float flicker = 0.78f + 0.22f * sind(anim * 180.0f);
        float len = 30.0f * flicker;
        float bloom = 0.9f + 0.1f * sind(anim * 300.0f);
        r.fill(1.0f, 0.55f, 0.16f, 0.20f);
        r.circle(0, 19, 13 * bloom);
        r.fill(1.0f, 0.75f, 0.35f, 0.27f);
        r.circle(0, 19, 7 * bloom);
        r.fill(0.90f, 0.27f, 0.08f, 0.47f);
        r.triangle(-9, 18, 0, 18 + len, 9, 18);
        r.fill(1.0f, 0.59f, 0.16f, 0.75f);
        r.triangle(-5, 18, 0, 18 + len * 0.85f, 5, 18);
        r.fill(1.0f, 0.92f, 0.55f, 0.94f);
        r.triangle(-2.5f, 18, 0, 18 + len * 0.55f, 2.5f, 18);
        r.setBlend(Renderer2D::Blend::Normal);
    }

    // Hull silhouette (narrow lens + nose spike + engine block) and swept wings,
    // straight from drawShipBody()'s point lists.
    const Vec2 hull[] = {
        {0, -37}, {-4, -29}, {-8, -18}, {-10, -6}, {-10, 6},
        {-12, 16}, {-12, 22}, {12, 22}, {12, 16}, {10, 6},
        {10, -6}, {8, -18}, {4, -29}
    };
    const Vec2 wingL[] = {{-8, -2}, {-34, 17}, {-30, 21}, {-11, 14}};
    const Vec2 wingR[] = {{8, -2}, {34, 17}, {30, 21}, {11, 14}};

    // Dark alien metal.
    r.fill(12.0f / 255.0f * 0.8f, 24.0f / 255.0f * 0.8f, 32.0f / 255.0f * 0.8f, 1.0f);
    r.convexPolygon(wingL, 4);
    r.convexPolygon(wingR, 4);

    r.fill(12.0f / 255.0f, 24.0f / 255.0f, 32.0f / 255.0f, 1.0f);
    // The hull is non-convex (nose spike), so fan it from a hub on the body axis.
    r.fanPolygon(0, -2, hull, 13, true);

    // Beveled crown highlight down the lit side.
    const Vec2 crown[] = {{0, -34}, {-6, -18}, {-7, -4}, {-6, 10}, {-2, 13}, {0, -6}};
    r.fill(36.0f / 255.0f, 62.0f / 255.0f, 78.0f / 255.0f, 1.0f);
    r.convexPolygon(crown, 6);

    // Glowing accents (additive): energy core, cockpit, engine glow.
    r.setBlend(Renderer2D::Blend::Additive);
    float pulse = 0.5f + 0.12f * sind(anim * 300.0f);
    r.fill(77.0f / 255.0f, 1.0f, 230.0f / 255.0f, 0.55f * pulse);
    r.circle(0, -4, 9);
    r.fill(0.78f, 1.0f, 0.94f, 0.85f);
    r.circle(0, -4, 2.5f);
    r.fill(140.0f / 255.0f, 1.0f, 217.0f / 255.0f, 0.7f);
    r.circle(0, -21, 4);
    r.fill(0.82f, 1.0f, 0.92f, 0.8f);
    r.circle(0, -21, 1.8f);
    if (thrustLevel > 0.01f) {
        r.fill(0.5f, 0.8f, 1.0f, 0.5f * thrustLevel);
        r.circle(0, 20, 12);
    }
    r.setBlend(Renderer2D::Blend::Normal);

    r.popMatrix();
}
