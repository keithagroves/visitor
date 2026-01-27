#include "chunked_terrain.h"
#include <cstdio>
#include <cmath>
#include <climits>

// Smooth interpolation between edges
static float smoothstep(float edge0, float edge1, float x) {
    x = fmaxf(0.0f, fminf(1.0f, (x - edge0) / (edge1 - edge0)));
    return x * x * (3.0f - 2.0f * x);
}

// ============================================================================
// TerrainChunk
// ============================================================================

TerrainChunk::TerrainChunk() {}

TerrainChunk::~TerrainChunk() {
    if (vao) glDeleteVertexArrays(1, &vao);
    if (vbo) glDeleteBuffers(1, &vbo);
    if (ebo) glDeleteBuffers(1, &ebo);
}

void TerrainChunk::generate(int cx, int cz, const PerlinNoise& noise) {
    chunkX = cx;
    chunkZ = cz;

    std::vector<float> vertices;
    std::vector<unsigned int> indices;

    float worldOffsetX = cx * WORLD_SIZE;
    float worldOffsetZ = cz * WORLD_SIZE;

    // Generate heightmap
    for (int z = 0; z <= SIZE; z++) {
        for (int x = 0; x <= SIZE; x++) {
            float worldX = worldOffsetX + (float)x / SIZE * WORLD_SIZE;
            float worldZ = worldOffsetZ + (float)z / SIZE * WORLD_SIZE;

            // Scale for noise sampling - smaller values = bigger features
            float nx = worldX * 0.004f;  // Large-scale mountains
            float nz = worldZ * 0.004f;

            // Multi-octave noise for varied terrain
            float h = noise.octaveNoise(nx, nz, 6, 0.5f);  // Base terrain
            h += noise.octaveNoise(nx * 3 + 100, nz * 3 + 100, 4, 0.5f) * 0.25f;  // Medium detail
            h += noise.octaveNoise(nx * 8 + 200, nz * 8 + 200, 3, 0.5f) * 0.1f;   // Fine detail
            h = (h + 1.0f) * 0.5f;
            h = powf(h, 1.5f);  // More contrast between valleys and peaks

            // Make low areas more gradual while keeping mountains steep
            // Raise the floor of valleys/low areas, compress the low range
            float lowThreshold = 0.35f;  // Below this, terrain gets raised up
            if (h < lowThreshold) {
                // Raise low areas - this makes valleys shallower and slopes gentler
                float t = h / lowThreshold;  // 0 to 1 range
                // Square the ratio to compress lows more (raises them toward threshold)
                float raised = t * t;
                // Blend between raised value and a minimum floor
                float minFloor = 0.08f;  // Minimum terrain height (keeps some water depth)
                h = minFloor + raised * (lowThreshold - minFloor);
            }

            // Create variable shoreline steepness
            // Use noise to vary how gradual the transition from water to land is
            float shoreNoise = noise.octaveNoise(nx * 2 + 400, nz * 2 + 400, 2, 0.5f);
            float shoreVariability = 0.3f + (shoreNoise + 1.0f) * 0.35f;  // Range 0.3 to 1.0

            // Apply gradual shoreline - flatten terrain near water level
            float waterLevel = 0.18f;  // Normalized water level
            if (h < waterLevel + 0.15f && h > waterLevel - 0.1f) {
                // Smoothly blend toward water level for gradual beaches
                float distFromWater = h - waterLevel;
                float beachWidth = 0.12f * shoreVariability;  // Variable beach width

                if (distFromWater > 0 && distFromWater < beachWidth) {
                    // Above water - create gradual slope up from beach
                    float t = distFromWater / beachWidth;
                    // Ease-in curve for gentle start, steeper further inland
                    float easedT = t * t * (3.0f - 2.0f * t) * shoreVariability;
                    h = waterLevel + easedT * beachWidth * 1.5f;
                }
            }

            // River generation - use separate noise layers for winding river paths
            // Rivers follow low-frequency noise patterns for natural meandering
            float riverNx = worldX * 0.001f;  // Very large scale for main river direction
            float riverNz = worldZ * 0.001f;

            // Create winding river paths using domain warping
            float warpX = noise.octaveNoise(riverNx + 500, riverNz + 500, 2, 0.5f) * 2.0f;
            float warpZ = noise.octaveNoise(riverNx + 700, riverNz + 700, 2, 0.5f) * 2.0f;

            // Main river noise - creates multiple river systems
            float river1 = noise.octaveNoise(riverNx * 3 + warpX, riverNz + warpZ, 2, 0.5f);
            float river2 = noise.octaveNoise(riverNx + warpZ, riverNz * 3 + warpX + 300, 2, 0.5f);

            // Wider river channels with gradual banks
            float riverWidth = 0.06f;  // Core river width
            float bankWidth = 0.12f;   // Gradual bank transition zone

            // Calculate distance from river center (0 = center, 1 = edge of bank)
            float dist1 = fabsf(river1);
            float dist2 = fabsf(river2);

            // River strength with gradual falloff for banks
            // Core river (full strength) -> bank transition -> normal terrain
            float river1Core = 1.0f - smoothstep(0.0f, riverWidth, dist1);
            float river1Bank = 1.0f - smoothstep(riverWidth, riverWidth + bankWidth, dist1);
            float river2Core = 1.0f - smoothstep(0.0f, riverWidth, dist2);
            float river2Bank = 1.0f - smoothstep(riverWidth, riverWidth + bankWidth, dist2);

            float riverCore = fmaxf(river1Core, river2Core);
            float riverBank = fmaxf(river1Bank, river2Bank);

            // Only carve rivers in lower elevations (not through mountain peaks)
            float elevationFactor = 1.0f - smoothstep(0.4f, 0.7f, h);
            riverCore *= elevationFactor;
            riverBank *= elevationFactor;

            // Gradual bank slopes - ease into the river
            // Banks slope gently, core is flat at water level
            float riverDepth = 0.12f;
            float bankDepth = riverBank * riverDepth * 0.6f;  // Banks are shallower
            float coreDepth = riverCore * riverDepth;

            // Apply smooth cubic easing for natural slope
            float bankEased = riverBank * riverBank * (3.0f - 2.0f * riverBank);
            h = h - bankEased * bankDepth - coreDepth;

            // Ensure river core is below water level
            if (riverCore > 0.6f) {
                h = fminf(h, 0.10f);  // Force below water line
            }

            heightMap[z][x] = h;
        }
    }

    // Generate vertices with normals
    for (int z = 0; z <= SIZE; z++) {
        for (int x = 0; x <= SIZE; x++) {
            float localX = (float)x / SIZE * WORLD_SIZE;
            float localZ = (float)z / SIZE * WORLD_SIZE;
            float h = heightMap[z][x];

            float worldX = worldOffsetX + localX;
            float worldZ = worldOffsetZ + localZ;
            float py = h * ChunkedTerrain::HEIGHT_SCALE - ChunkedTerrain::TERRAIN_OFFSET;

            // Calculate normal
            float hL = x > 0 ? heightMap[z][x-1] : h;
            float hR = x < SIZE ? heightMap[z][x+1] : h;
            float hD = z > 0 ? heightMap[z-1][x] : h;
            float hU = z < SIZE ? heightMap[z+1][x] : h;

            Vec3 normal = Vec3(hL - hR, 2.0f / SIZE * 10, hD - hU).normalize();

            // Position (3) + Normal (3) + Height (1)
            vertices.push_back(worldX);
            vertices.push_back(py);
            vertices.push_back(worldZ);
            vertices.push_back(normal.x);
            vertices.push_back(normal.y);
            vertices.push_back(normal.z);
            vertices.push_back(h * 2.0f - 0.3f);
        }
    }

    // Generate indices
    for (int z = 0; z < SIZE; z++) {
        for (int x = 0; x < SIZE; x++) {
            int topLeft = z * (SIZE + 1) + x;
            int topRight = topLeft + 1;
            int bottomLeft = (z + 1) * (SIZE + 1) + x;
            int bottomRight = bottomLeft + 1;

            indices.push_back(topLeft);
            indices.push_back(bottomLeft);
            indices.push_back(topRight);

            indices.push_back(topRight);
            indices.push_back(bottomLeft);
            indices.push_back(bottomRight);
        }
    }

    indexCount = indices.size();

    // Create GPU buffers
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ebo);

    glBindVertexArray(vao);

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);
}

void TerrainChunk::render() {
    glBindVertexArray(vao);
    glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, 0);
}

float TerrainChunk::getHeightAt(float localX, float localZ) const {
    float gridX = localX / WORLD_SIZE * SIZE;
    float gridZ = localZ / WORLD_SIZE * SIZE;

    if (gridX < 0) gridX = 0;
    if (gridZ < 0) gridZ = 0;
    if (gridX >= SIZE) gridX = SIZE - 0.001f;
    if (gridZ >= SIZE) gridZ = SIZE - 0.001f;

    int x0 = (int)gridX;
    int z0 = (int)gridZ;
    int x1 = x0 + 1;
    int z1 = z0 + 1;

    if (x1 > SIZE) x1 = SIZE;
    if (z1 > SIZE) z1 = SIZE;

    float fx = gridX - x0;
    float fz = gridZ - z0;

    float h00 = heightMap[z0][x0];
    float h10 = heightMap[z0][x1];
    float h01 = heightMap[z1][x0];
    float h11 = heightMap[z1][x1];

    float h0 = h00 * (1 - fx) + h10 * fx;
    float h1 = h01 * (1 - fx) + h11 * fx;

    return h0 * (1 - fz) + h1 * fz;
}

// ============================================================================
// ChunkedTerrain
// ============================================================================

ChunkedTerrain::ChunkedTerrain() : noise(42) {}

ChunkedTerrain::~ChunkedTerrain() {
    for (auto& pair : chunks) {
        delete pair.second;
    }
    chunks.clear();
}

void ChunkedTerrain::init(unsigned int seed) {
    noise = PerlinNoise(seed);
    treeManager.init(seed);
    houseManager.init(seed);
    printf("Chunked terrain initialized (seed: %u, render distance: %d chunks)\n",
           seed, RENDER_DISTANCE);
}

std::pair<int, int> ChunkedTerrain::worldToChunk(float worldX, float worldZ) {
    int cx = (int)floorf(worldX / TerrainChunk::WORLD_SIZE);
    int cz = (int)floorf(worldZ / TerrainChunk::WORLD_SIZE);
    return {cx, cz};
}

void ChunkedTerrain::loadChunk(int chunkX, int chunkZ) {
    auto key = std::make_pair(chunkX, chunkZ);
    if (chunks.find(key) != chunks.end()) {
        return;  // Already loaded
    }

    TerrainChunk* chunk = new TerrainChunk();
    chunk->generate(chunkX, chunkZ, noise);
    chunks[key] = chunk;

    // Generate trees and houses for this chunk using raw height data (0-1 range)
    const float* rawHeights = chunk->getHeightData();
    int res = TerrainChunk::getResolution();
    treeManager.updateForChunk(chunkX, chunkZ, rawHeights, res, TerrainChunk::WORLD_SIZE);
    houseManager.updateForChunk(chunkX, chunkZ, rawHeights, res, TerrainChunk::WORLD_SIZE);
}

void ChunkedTerrain::unloadDistantChunks(int centerX, int centerZ) {
    std::vector<std::pair<int, int>> toRemove;

    for (auto& pair : chunks) {
        int dx = pair.first.first - centerX;
        int dz = pair.first.second - centerZ;

        // Unload if outside render distance + 1 buffer
        if (abs(dx) > RENDER_DISTANCE + 1 || abs(dz) > RENDER_DISTANCE + 1) {
            toRemove.push_back(pair.first);
        }
    }

    for (auto& key : toRemove) {
        delete chunks[key];
        chunks.erase(key);
        treeManager.clearChunk(key.first, key.second);
        houseManager.clearChunk(key.first, key.second);
    }
}

void ChunkedTerrain::update(float cameraX, float cameraZ) {
    auto [centerX, centerZ] = worldToChunk(cameraX, cameraZ);

    // Only update if camera moved to a new chunk
    if (centerX == lastCameraChunkX && centerZ == lastCameraChunkZ) {
        return;
    }

    lastCameraChunkX = centerX;
    lastCameraChunkZ = centerZ;

    // Load chunks around camera
    for (int dz = -RENDER_DISTANCE; dz <= RENDER_DISTANCE; dz++) {
        for (int dx = -RENDER_DISTANCE; dx <= RENDER_DISTANCE; dx++) {
            loadChunk(centerX + dx, centerZ + dz);
        }
    }

    // Unload distant chunks
    unloadDistantChunks(centerX, centerZ);
}

void ChunkedTerrain::render(GLuint program, const Mat4& mvp, const Mat4& model,
                            const Vec3& lightDir, const Vec3& cameraPos, float time) {
    glUseProgram(program);
    glUniformMatrix4fv(glGetUniformLocation(program, "uMVP"), 1, GL_FALSE, mvp.m);
    glUniformMatrix4fv(glGetUniformLocation(program, "uModel"), 1, GL_FALSE, model.m);
    glUniform3f(glGetUniformLocation(program, "uLightDir"), lightDir.x, lightDir.y, lightDir.z);
    glUniform3f(glGetUniformLocation(program, "uCameraPos"), cameraPos.x, cameraPos.y, cameraPos.z);
    glUniform1f(glGetUniformLocation(program, "uTime"), time);

    for (auto& pair : chunks) {
        pair.second->render();
    }
}

void ChunkedTerrain::renderTrees(GLuint program, const Mat4& viewProjection,
                                  const Vec3& lightDir, const Vec3& cameraPos, float time) {
    treeManager.render(program, viewProjection, lightDir, cameraPos, time);
}

void ChunkedTerrain::renderHouses(GLuint program, const Mat4& viewProjection,
                                   const Vec3& lightDir, const Vec3& cameraPos, float time) {
    houseManager.render(program, viewProjection, lightDir, cameraPos, time);
}

float ChunkedTerrain::getHeightAt(float worldX, float worldZ) const {
    auto [chunkX, chunkZ] = worldToChunk(worldX, worldZ);
    auto key = std::make_pair(chunkX, chunkZ);

    auto it = chunks.find(key);
    if (it == chunks.end()) {
        return 0.0f;  // Chunk not loaded
    }

    float localX = worldX - chunkX * TerrainChunk::WORLD_SIZE;
    float localZ = worldZ - chunkZ * TerrainChunk::WORLD_SIZE;

    float h = it->second->getHeightAt(localX, localZ);
    return h * HEIGHT_SCALE - TERRAIN_OFFSET;
}
