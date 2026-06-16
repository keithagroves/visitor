#include "terrain_field.h"
#include <cmath>
#include <algorithm>

namespace {
// Sample a uniform angle->radius LUT (index 0 == 0 deg) with linear wrap.
float sampleLUT(const std::vector<float>& lut, float angleDeg) {
    int n = (int)lut.size();
    if (n == 0) return 0.0f;
    float a = angleDeg;
    while (a < 0.0f) a += 360.0f;
    while (a >= 360.0f) a -= 360.0f;
    float f = a / 360.0f * n;
    int i0 = (int)f % n;
    int i1 = (i0 + 1) % n;
    float frac = f - floorf(f);
    return lut[i0] * (1.0f - frac) + lut[i1] * frac;
}
}

void TerrainField::generate(Vec2 center, float coreRadius, float outerRadius,
                            float cellSize, const std::vector<float>& seedLUT) {
    center_ = center;
    coreR_ = coreRadius;
    outerR_ = outerRadius;
    cell_ = cellSize;

    // Square grid covering [-outerR, +outerR] around the centre.
    nx_ = (int)std::ceil(2.0f * outerR_ / cell_);
    ny_ = nx_;
    ox_ = center_.x - outerR_;
    oy_ = center_.y - outerR_;

    d_.assign((size_t)(nx_ + 1) * (ny_ + 1), 0.0f);
    for (int j = 0; j <= ny_; j++) {
        for (int i = 0; i <= nx_; i++) {
            Vec2 p = cornerPos(i, j);
            float dx = p.x - center_.x, dy = p.y - center_.y;
            float dist = sqrtf(dx * dx + dy * dy);
            float surf = sampleLUT(seedLUT, atan2d(dy, dx));
            // Positive (solid) below the surface, negative (open) above it.
            corner(i, j) = surf - dist;
        }
    }
    digRect_ = Rect{};
}

bool TerrainField::dig(Vec2 world, float radius) {
    // Touch only corners the brush can reach, and never carve into the core.
    int i0 = std::max(0, (int)std::floor((world.x - radius - ox_) / cell_));
    int i1 = std::min(nx_, (int)std::ceil((world.x + radius - ox_) / cell_));
    int j0 = std::max(0, (int)std::floor((world.y - radius - oy_) / cell_));
    int j1 = std::min(ny_, (int)std::ceil((world.y + radius - oy_) / cell_));
    float coreFloor = coreR_ + cell_;
    bool changed = false;
    for (int j = j0; j <= j1; j++) {
        for (int i = i0; i <= i1; i++) {
            Vec2 p = cornerPos(i, j);
            float cdx = p.x - center_.x, cdy = p.y - center_.y;
            if (sqrtf(cdx * cdx + cdy * cdy) < coreFloor) continue;  // protect the core
            float bx = p.x - world.x, by = p.y - world.y;
            float carved = sqrtf(bx * bx + by * by) - radius;  // <0 inside the brush
            float& c = corner(i, j);
            if (carved < c) { c = carved; changed = true; }
        }
    }
    digRect_ = changed ? Rect{i0, j0, i1, j1} : Rect{};
    return changed;
}

float TerrainField::densityAt(Vec2 world) const {
    float gi = (world.x - ox_) / cell_;
    float gj = (world.y - oy_) / cell_;
    if (gi < 0 || gj < 0 || gi >= nx_ || gj >= ny_) return -1.0f;  // outside -> open
    int i = (int)gi, j = (int)gj;
    float fx = gi - i, fy = gj - j;
    float a = cornerVal(i, j),     b = cornerVal(i + 1, j);
    float c = cornerVal(i, j + 1), e = cornerVal(i + 1, j + 1);
    float top = a * (1 - fx) + b * fx;
    float bot = c * (1 - fx) + e * fx;
    return top * (1 - fy) + bot * fy;
}

float TerrainField::outerSurfaceRadius(float angleDeg) const {
    Vec2 dir(cosd(angleDeg), sind(angleDeg));
    float step = cell_ * 0.5f;
    float prevR = outerR_;
    float prevD = densityAt(center_ + dir * prevR);
    for (float r = outerR_ - step; r >= coreR_; r -= step) {
        float dens = densityAt(center_ + dir * r);
        if (dens >= 0.0f) {
            // Crossed from open (prevD<0) to solid: interpolate the 0 crossing.
            if (prevD < 0.0f) {
                float t = prevD / (prevD - dens);
                return prevR + (r - prevR) * t;
            }
            return r;  // already solid at the top sample
        }
        prevR = r;
        prevD = dens;
    }
    return coreR_;
}
