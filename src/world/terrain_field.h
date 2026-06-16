#pragma once

#include <vector>
#include "../math/vecmath.h"

// Destructible 2D density grid for the planet crust. Each grid CORNER stores a
// signed density: > 0 is solid rock, < 0 is open (sky or a dug cave), and the 0
// iso-line is the rock surface. The field is seeded from the planet's
// heightfield; digging carves the field (pushes corner densities negative
// inside a brush).
//
// The grid is the source of truth for collision (solidAt) and the silhouette
// LUT (outerSurfaceRadius). Rendering samples the same grid on the GPU: the
// corner array is mirrored into an R16F texture and a fullscreen "body" shader
// smoothsteps the 0 iso-line per pixel (see main.cpp). After a dig, only the
// brush's dirty rect needs re-uploading — digRect() reports it in corner
// indices so the caller can glTexSubImage2D just that region.
//
// Lives in world coordinates but is organized around the planet centre. Only
// the crust shell [coreRadius .. outerRadius] is diggable; everything below
// coreRadius is protected and stays solid.
class TerrainField {
public:
    // Build the grid covering the disk of `outerRadius` around `center`, seeding
    // density = seedSurface(angle) - dist so rock fills in below the silhouette.
    // `seedLUT` is a uniform angle->surfaceRadius table (index 0 == 0 degrees).
    void generate(Vec2 center, float coreRadius, float outerRadius, float cellSize,
                  const std::vector<float>& seedLUT);

    // Carve rock out of a circular brush (world space). Won't dig below the core.
    // Returns true if any corner changed (digRect() then bounds the change).
    bool dig(Vec2 world, float radius);

    bool solidAt(Vec2 world) const { return densityAt(world) >= 0.0f; }

    // Radius of the outermost rock surface along an angle (degrees): marches in
    // from the top, so craters lower it but a sealed tunnel underneath does not.
    float outerSurfaceRadius(float angleDeg) const;

    Vec2 center() const { return center_; }

    // --- GPU mirror -----------------------------------------------------------
    // Corner grid dimensions/placement, for uploading d_ as a texture and for
    // mapping world coords to texels in the body shader.
    int cornersX() const { return nx_ + 1; }
    int cornersY() const { return ny_ + 1; }
    float originX() const { return ox_; }
    float originY() const { return oy_; }
    float cellSize() const { return cell_; }
    const std::vector<float>& corners() const { return d_; }

    // Inclusive corner-index bounds touched by the most recent successful dig.
    struct Rect { int i0 = 0, j0 = 0, i1 = -1, j1 = -1; };
    Rect digRect() const { return digRect_; }

private:
    float& corner(int i, int j) { return d_[(size_t)j * (nx_ + 1) + i]; }
    float cornerVal(int i, int j) const { return d_[(size_t)j * (nx_ + 1) + i]; }
    Vec2 cornerPos(int i, int j) const { return Vec2(ox_ + i * cell_, oy_ + j * cell_); }
    float densityAt(Vec2 world) const;      // bilinear sample

    Vec2 center_;
    float coreR_ = 0, outerR_ = 0, cell_ = 1.0f;
    int nx_ = 0, ny_ = 0;                    // cell counts; corners are (nx+1)x(ny+1)
    float ox_ = 0, oy_ = 0;                  // world position of corner (0,0)
    std::vector<float> d_;                   // corner densities
    Rect digRect_;                           // corners touched by the last dig
};
