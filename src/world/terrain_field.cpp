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
            // Crossed from open (prevD<0) to solid. The density between the
            // two samples is not a straight line (it is bilinear across each
            // cell the ray passes through), so interpolating them lands
            // several units off the real 0 line. Bisect for it: this outline
            // has to agree with the ground the shader draws from the same grid.
            if (prevD < 0.0f) {
                float open = prevR, solid = r;
                for (int k = 0; k < 20; k++) {
                    float mid = 0.5f * (open + solid);
                    if (densityAt(center_ + dir * mid) >= 0.0f) solid = mid;
                    else open = mid;
                }
                return 0.5f * (open + solid);
            }
            return r;  // already solid at the top sample
        }
        prevR = r;
        prevD = dens;
    }
    return coreR_;
}
