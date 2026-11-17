#pragma once

#include <vector>
#include "../math/vecmath.h"

// Coarse 2D density grid for the planet crust. Each grid CORNER stores a signed
// density: > 0 is solid rock, < 0 is open sky, and the 0 iso-line is the rock
// surface. The field is seeded from the planet's heightfield.
//
// It is the shape of the terrain at large scale: the silhouette LUT is marched
// out of it (outerSurfaceRadius), and the body shader renders it by sampling
// the same grid on the GPU (the corner array is mirrored into an R16F texture
// and the 0 iso-line is anti-aliased per pixel — see main.cpp).
//
// Digging does NOT edit this grid — its cells are far larger than a person.
// Holes live in a separate fine layer, CarveMap; the rock that is really there
// is the minimum of the two (Planet::rockAt).
//
// Lives in world coordinates but is organized around the planet centre.
class TerrainField {
public:
    // Build the grid covering the disk of `outerRadius` around `center`, seeding
    // density = seedSurface(angle) - dist so rock fills in below the silhouette.
    // `seedLUT` is a uniform angle->surfaceRadius table (index 0 == 0 degrees).
    void generate(Vec2 center, float coreRadius, float outerRadius, float cellSize,
                  const std::vector<float>& seedLUT);

    // Translate the whole grid (e.g. the planet moved along its orbit). Pure
    // origin shift: corner data is center-relative by construction. The origin
    // is recomputed from the center every time rather than nudged by the frame
    // delta — a planet that orbits for an hour would otherwise accumulate
    // float rounding and let the rendered rock drift off the collision shape.
    void moveTo(Vec2 newCenter) {
        center_ = newCenter;
        ox_ = center_.x - outerR_;
        oy_ = center_.y - outerR_;
    }

    // Free the grid memory (lazy fields: planet left behind). generate() again
    // to rebuild.
    void release() { d_.clear(); d_.shrink_to_fit(); nx_ = ny_ = 0; }

    // Signed density at a world point (bilinear): > 0 rock, < 0 open. Reads as
    // open outside the grid.
    float density(Vec2 world) const { return densityAt(world); }
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
};
