#include "astronaut.h"
#include <cmath>
#include <cstdlib>
#include <vector>

namespace {
constexpr int FRAME_W = 48;
constexpr int FRAME_H = 64;
constexpr int FRAME_COUNT = 4;
GLuint gAstronautSheet = 0;

struct Rgba {
    unsigned char r, g, b, a;
};

void putPixel(std::vector<unsigned char>& px, int w, int h, int x, int y, Rgba c) {
    if (x < 0 || y < 0 || x >= w || y >= h || c.a == 0) return;
    int i = (y * w + x) * 4;
    float srcA = c.a / 255.0f;
    float dstA = px[i + 3] / 255.0f;
    float outA = srcA + dstA * (1.0f - srcA);
    if (outA <= 0.0f) return;
    px[i + 0] = (unsigned char)((c.r * srcA + px[i + 0] * dstA * (1.0f - srcA)) / outA);
    px[i + 1] = (unsigned char)((c.g * srcA + px[i + 1] * dstA * (1.0f - srcA)) / outA);
    px[i + 2] = (unsigned char)((c.b * srcA + px[i + 2] * dstA * (1.0f - srcA)) / outA);
    px[i + 3] = (unsigned char)(outA * 255.0f);
}

void rect(std::vector<unsigned char>& px, int w, int h, int x0, int y0, int x1, int y1, Rgba c) {
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) putPixel(px, w, h, x, y, c);
    }
}

void circle(std::vector<unsigned char>& px, int w, int h, int cx, int cy, int r, Rgba c) {
    int rr = r * r;
    for (int y = cy - r; y <= cy + r; y++) {
        for (int x = cx - r; x <= cx + r; x++) {
            int dx = x - cx, dy = y - cy;
            if (dx * dx + dy * dy <= rr) putPixel(px, w, h, x, y, c);
        }
    }
}

void ellipse(std::vector<unsigned char>& px, int w, int h, int cx, int cy, int rx, int ry, Rgba c) {
    int rr = rx * rx * ry * ry;
    for (int y = cy - ry; y <= cy + ry; y++) {
        for (int x = cx - rx; x <= cx + rx; x++) {
            int dx = x - cx, dy = y - cy;
            if (dx * dx * ry * ry + dy * dy * rx * rx <= rr) putPixel(px, w, h, x, y, c);
        }
    }
}

void line(std::vector<unsigned char>& px, int w, int h, int x0, int y0, int x1, int y1, int radius, Rgba c) {
    int dx = x1 - x0, dy = y1 - y0;
    int steps = std::abs(dx) > std::abs(dy) ? std::abs(dx) : std::abs(dy);
    if (steps < 1) {
        circle(px, w, h, x0, y0, radius, c);
        return;
    }
    for (int i = 0; i <= steps; i++) {
        float t = (float)i / (float)steps;
        int x = (int)roundf(x0 + dx * t);
        int y = (int)roundf(y0 + dy * t);
        circle(px, w, h, x, y, radius, c);
    }
}

void drawSpriteFrame(std::vector<unsigned char>& px, int sheetW, int sheetH, int frame) {
    int ox = frame * FRAME_W;
    int phase = frame == 0 ? 0 : frame == 1 ? 1 : frame == 2 ? 0 : -1;
    int bob = (frame == 1 || frame == 3) ? -2 : 0;

    Rgba outline{20, 25, 42, 255};
    Rgba boot{26, 31, 56, 255};
    Rgba legBack{38, 48, 78, 255};
    Rgba legFront{65, 82, 128, 255};
    Rgba pack{72, 82, 101, 255};
    Rgba packDark{48, 58, 78, 255};
    Rgba suit{205, 212, 224, 255};
    Rgba suitShadow{154, 164, 184, 255};
    Rgba stripe{98, 120, 156, 255};
    Rgba visor{18, 28, 52, 245};
    Rgba visorGlow{108, 170, 255, 150};

    auto X = [ox](int x) { return ox + x; };
    auto Y = [bob](int y) { return y + bob; };

    ellipse(px, sheetW, sheetH, X(24), 56, 16, 4, Rgba{0, 0, 0, 70});

    rect(px, sheetW, sheetH, X(31), Y(25), X(40), Y(43), outline);
    rect(px, sheetW, sheetH, X(32), Y(24), X(38), Y(42), pack);
    rect(px, sheetW, sheetH, X(37), Y(28), X(42), Y(38), packDark);

    int front = phase;
    int back = -phase;
    int hipLx = X(20), hipRx = X(28), hipY = Y(39);
    int kneeLx = X(19 - front * 5), kneeLy = Y(49);
    int footLx = X(17 - front * 9), footLy = Y(58 - std::abs(front) * 2);
    int kneeRx = X(29 - back * 5), kneeRy = Y(49);
    int footRx = X(31 - back * 9), footRy = Y(58 - std::abs(back) * 2);
    line(px, sheetW, sheetH, hipRx, hipY, kneeRx, kneeRy, 4, outline);
    line(px, sheetW, sheetH, kneeRx, kneeRy, footRx, footRy, 4, outline);
    line(px, sheetW, sheetH, hipRx, hipY, kneeRx, kneeRy, 3, legBack);
    line(px, sheetW, sheetH, kneeRx, kneeRy, footRx, footRy, 3, legBack);
    line(px, sheetW, sheetH, hipLx, hipY, kneeLx, kneeLy, 4, outline);
    line(px, sheetW, sheetH, kneeLx, kneeLy, footLx, footLy, 4, outline);
    line(px, sheetW, sheetH, hipLx, hipY, kneeLx, kneeLy, 3, legFront);
    line(px, sheetW, sheetH, kneeLx, kneeLy, footLx, footLy, 3, legFront);
    line(px, sheetW, sheetH, footRx - 5, footRy, footRx + 5, footRy, 3, boot);
    line(px, sheetW, sheetH, footLx - 5, footLy, footLx + 6, footLy, 3, boot);

    int shoulderLx = X(15), shoulderRx = X(33), shoulderY = Y(25);
    int elbowLx = X(10 + back * 3), elbowLy = Y(35);
    int handLx = X(12 + back * 7), handLy = Y(45);
    int elbowRx = X(38 + front * 3), elbowRy = Y(35);
    int handRx = X(36 + front * 7), handRy = Y(45);
    line(px, sheetW, sheetH, shoulderRx, shoulderY, elbowRx, elbowRy, 3, outline);
    line(px, sheetW, sheetH, elbowRx, elbowRy, handRx, handRy, 3, outline);
    line(px, sheetW, sheetH, shoulderLx, shoulderY, elbowLx, elbowLy, 3, outline);
    line(px, sheetW, sheetH, elbowLx, elbowLy, handLx, handLy, 3, outline);

    ellipse(px, sheetW, sheetH, X(24), Y(31), 11, 15, outline);
    ellipse(px, sheetW, sheetH, X(24), Y(31), 9, 13, suit);
    rect(px, sheetW, sheetH, X(18), Y(33), X(30), Y(38), stripe);
    rect(px, sheetW, sheetH, X(29), Y(26), X(32), Y(38), suitShadow);

    line(px, sheetW, sheetH, shoulderRx, shoulderY, elbowRx, elbowRy, 2, suitShadow);
    line(px, sheetW, sheetH, elbowRx, elbowRy, handRx, handRy, 2, suitShadow);
    line(px, sheetW, sheetH, shoulderLx, shoulderY, elbowLx, elbowLy, 2, suit);
    line(px, sheetW, sheetH, elbowLx, elbowLy, handLx, handLy, 2, suit);
    circle(px, sheetW, sheetH, handRx, handRy, 4, outline);
    circle(px, sheetW, sheetH, handLx, handLy, 4, outline);
    circle(px, sheetW, sheetH, handRx, handRy, 3, legFront);
    circle(px, sheetW, sheetH, handLx, handLy, 3, legFront);

    ellipse(px, sheetW, sheetH, X(24), Y(18), 10, 5, outline);
    ellipse(px, sheetW, sheetH, X(24), Y(18), 8, 3, suitShadow);
    circle(px, sheetW, sheetH, X(24), Y(10), 14, outline);
    circle(px, sheetW, sheetH, X(24), Y(10), 12, suit);
    ellipse(px, sheetW, sheetH, X(24), Y(10), 9, 7, visor);
    ellipse(px, sheetW, sheetH, X(20), Y(8), 4, 2, visorGlow);
}

GLuint astronautSheet(Renderer2D& r) {
    if (gAstronautSheet) return gAstronautSheet;
    int sheetW = FRAME_W * FRAME_COUNT;
    int sheetH = FRAME_H;
    std::vector<unsigned char> pixels(sheetW * sheetH * 4, 0);
    for (int i = 0; i < FRAME_COUNT; i++) drawSpriteFrame(pixels, sheetW, sheetH, i);
    gAstronautSheet = r.createTextureRGBA(sheetW, sheetH, pixels.data(), true);
    return gAstronautSheet;
}
}

void Astronaut::spawn(Vec2 position) {
    pos           = position;
    vel           = Vec2(0, 0);
    walkAnim      = 0.0f;
    idleAnim      = 0.0f;
    facing        = 1;
    grounded      = false;
    walking       = false;
    jetting       = false;
    walkRequested = false;
    jetpackFuel   = JETPACK_MAX_FUEL;
    active        = true;
}

void Astronaut::update(const Planet& planet, float timeScale) {
    if (!active) return;

    idleAnim += timeScale * 0.08f;

    // 1/r² gravity toward planet center (same formula as the cow's Falling state).
    float dx = planet.center.x - pos.x;
    float dy = planet.center.y - pos.y;
    float distSq = dx * dx + dy * dy;
    if (distSq < 1.0f) distSq = 1.0f;
    float gravD = sqrtf(distSq);
    float force = planet.gravity / distSq;
    vel.x += (dx / gravD) * force * timeScale;
    vel.y += (dy / gravD) * force * timeScale;

    pos.x += vel.x * timeScale;
    pos.y += vel.y * timeScale;

    // Ground detection — recompute distance from the updated position.
    float pdx = pos.x - planet.center.x;
    float pdy = pos.y - planet.center.y;
    float d    = sqrtf(pdx * pdx + pdy * pdy);
    if (d < 1.0f) d = 1.0f;
    float outX = pdx / d;
    float outY = pdy / d;

    float ang   = atan2d(pdy, pdx);
    float surfR = planet.surfaceRadius(ang);

    grounded = d <= surfR + RADIUS + 2.0f;
    if (grounded) {
        // Push out to surface.
        pos.x = planet.center.x + outX * (surfR + RADIUS);
        pos.y = planet.center.y + outY * (surfR + RADIUS);

        // Kill inward radial velocity.
        float radVel = vel.x * outX + vel.y * outY;
        if (radVel < 0.0f) {
            vel.x -= radVel * outX;
            vel.y -= radVel * outY;
        }

        // Surface friction damps tangential drift.
        vel.x *= WALK_DAMP;
        vel.y *= WALK_DAMP;

        // Recharge jetpack fuel on the ground.
        float charged = jetpackFuel + JETPACK_CHARGE * timeScale;
        jetpackFuel = charged < JETPACK_MAX_FUEL ? charged : JETPACK_MAX_FUEL;
    }

    walking = grounded && walkRequested;
    if (walking) {
        walkAnim += timeScale * 0.28f;
    } else {
        walkAnim += timeScale * 0.05f;
    }
    walkRequested = false;
}

void Astronaut::walkLeft(const Planet& planet, float timeScale) {
    float dx = pos.x - planet.center.x;
    float dy = pos.y - planet.center.y;
    float d  = sqrtf(dx * dx + dy * dy);
    if (d < 1.0f) return;
    // This tangent resolves to screen-right after the camera rolls gravity down.
    float tx = -dy / d;
    float ty =  dx / d;
    vel.x -= tx * WALK_FORCE * timeScale;
    vel.y -= ty * WALK_FORCE * timeScale;
    facing = -1;
    walkRequested = true;
}

void Astronaut::walkRight(const Planet& planet, float timeScale) {
    float dx = pos.x - planet.center.x;
    float dy = pos.y - planet.center.y;
    float d  = sqrtf(dx * dx + dy * dy);
    if (d < 1.0f) return;
    float tx = -dy / d;
    float ty =  dx / d;
    vel.x += tx * WALK_FORCE * timeScale;
    vel.y += ty * WALK_FORCE * timeScale;
    facing = 1;
    walkRequested = true;
}

void Astronaut::jump(const Planet& planet) {
    if (!grounded) return;
    float dx = pos.x - planet.center.x;
    float dy = pos.y - planet.center.y;
    float d  = sqrtf(dx * dx + dy * dy);
    if (d < 1.0f) return;
    vel.x += (dx / d) * JUMP_SPEED;
    vel.y += (dy / d) * JUMP_SPEED;
    grounded = false;
}

void Astronaut::jetpack(const Planet& planet, float timeScale) {
    if (jetpackFuel <= 0.0f) return;
    float dx = pos.x - planet.center.x;
    float dy = pos.y - planet.center.y;
    float d  = sqrtf(dx * dx + dy * dy);
    if (d < 1.0f) return;
    vel.x += (dx / d) * JETPACK_THRUST * timeScale;
    vel.y += (dy / d) * JETPACK_THRUST * timeScale;
    jetpackFuel -= JETPACK_DRAIN * timeScale;
    if (jetpackFuel < 0.0f) jetpackFuel = 0.0f;
    jetting = true;
}

void Astronaut::draw(Renderer2D& r, const Planet& planet) const {
    if (!active) return;

    // Stand upright: local +Y points toward planet center (feet down), same
    // convention as the cow's standAngle.
    float standAngle = atan2d(pos.y - planet.center.y, pos.x - planet.center.x) + 90.0f;

    r.pushMatrix();
    r.translate(pos.x, pos.y);
    r.rotate(standAngle);
    r.scale(SCALE);

    int frame = walking ? ((int)floorf(walkAnim) & 3) : 0;
    float idleBob = walking ? 0.0f : sinf(idleAnim) * 0.8f;

    // Shadow/ground contact keeps the little sprite readable against terrain.
    r.fill(0.02f, 0.03f, 0.05f, grounded ? 0.28f : 0.12f);
    r.ellipse(0, 30, 18, 4, 16);

    // Flame — drawn behind the sprite so the suit silhouette stays crisp.
    if (jetting) {
        r.setBlend(Renderer2D::Blend::Additive);
        r.fill(1.0f, 0.30f, 0.0f, 0.22f);
        r.ellipse(0, 34, 7, 13, 10);
        r.fill(1.0f, 0.78f, 0.0f, 0.50f);
        r.ellipse(0, 31, 4, 8, 10);
        r.fill(1.0f, 1.0f, 0.85f, 0.82f);
        r.ellipse(0, 27, 2, 4, 8);
        r.setBlend(Renderer2D::Blend::Normal);
    }

    Renderer2D::SpriteFrame src{(float)(frame * FRAME_W), 0.0f, (float)FRAME_W, (float)FRAME_H};
    r.sprite(astronautSheet(r), FRAME_W * FRAME_COUNT, FRAME_H, src,
             -24.0f, -42.0f + idleBob, 48.0f, 64.0f,
             Renderer2D::Color{1, 1, 1, 1}, facing < 0, false);

    r.popMatrix();
}
