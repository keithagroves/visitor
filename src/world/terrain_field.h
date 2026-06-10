#pragma once

#include <vector>
#include "../math/vecmath.h"

// Destructible 2D density grid for the planet crust, meshed with marching
// squares. Each grid CORNER stores a signed density: > 0 is solid rock, < 0 is
// open (sky or a dug cave), and the 0 iso-line is the rock surface. The field is
// seeded from the planet's heightfield; digging carves the field (pushes corner
// densities negative inside a brush), which flags it dirty and re-meshes.
//
// Lives in world coordinates but is organized around the planet centre. Only the
// crust shell [coreRadius .. outerRadius] is meshed; everything below coreRadius
// is always-solid and filled cheaply by a fan in the mesh, so digging is bounded
// to the crust and the interior never needs per-cell geometry.
class TerrainField {
public:
    // Build the grid covering the disk of `outerRadius` around `center`, seeding
    // density = seedSurface(angle) - dist so rock fills in below the silhouette.
    // `seedLUT` is a uniform angle->surfaceRadius table (index 0 == 0 degrees).
    void generate(Vec2 center, float coreRadius, float outerRadius, float cellSize,
                  const std::vector<float>& seedLUT);

    // Carve rock out of a circular brush (world space). Won't dig below the core.
    void dig(Vec2 world, float radius);

    bool solidAt(Vec2 world) const { return densityAt(world) >= 0.0f; }

    // Radius of the outermost rock surface along an angle (degrees): marches in
    // from the top, so craters lower it but a sealed tunnel underneath does not.
    float outerSurfaceRadius(float angleDeg) const;

    // Marching-squares triangle soup in WORLD coords (x,y interleaved). Cached;
    // rebuilt on demand when dirty.
    const std::vector<float>& mesh();
    bool dirty() const { return dirty_; }

    Vec2 center() const { return center_; }

private:
    float& corner(int i, int j) { return d_[(size_t)j * (nx_ + 1) + i]; }
    float cornerVal(int i, int j) const { return d_[(size_t)j * (nx_ + 1) + i]; }
    Vec2 cornerPos(int i, int j) const { return Vec2(ox_ + i * cell_, oy_ + j * cell_); }
    float densityAt(Vec2 world) const;      // bilinear sample
    void buildMesh();

    Vec2 center_;
    float coreR_ = 0, outerR_ = 0, cell_ = 1.0f;
    int nx_ = 0, ny_ = 0;                    // cell counts; corners are (nx+1)x(ny+1)
    float ox_ = 0, oy_ = 0;                  // world position of corner (0,0)
    std::vector<float> d_;                   // corner densities
    std::vector<float> mesh_;                // cached world-space triangles
    bool dirty_ = true;
};
