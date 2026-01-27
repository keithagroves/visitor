#pragma once

#include <GLES3/gl3.h>
#include <unordered_map>
#include <vector>
#include "../math/vecmath.h"
#include "../math/noise.h"

// Single terrain chunk
class TerrainChunk {
public:
    static constexpr int SIZE = 64;  // Vertices per side (more detail)
    static constexpr float WORLD_SIZE = 128.0f;  // World units per chunk (bigger chunks)

    TerrainChunk();
    ~TerrainChunk();

    void generate(int chunkX, int chunkZ, const PerlinNoise& noise);
    void render();
    float getHeightAt(float localX, float localZ) const;

    int getChunkX() const { return chunkX; }
    int getChunkZ() const { return chunkZ; }

private:
    int chunkX = 0;
    int chunkZ = 0;

    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint ebo = 0;
    int indexCount = 0;

    float heightMap[SIZE + 1][SIZE + 1];
};

// Hash function for chunk coordinates
struct ChunkCoordHash {
    std::size_t operator()(const std::pair<int, int>& coord) const {
        return std::hash<int>()(coord.first) ^ (std::hash<int>()(coord.second) << 16);
    }
};

// Manages all terrain chunks
class ChunkedTerrain {
public:
    static constexpr float HEIGHT_SCALE = 80.0f;  // Taller mountains
    static constexpr int RENDER_DISTANCE = 5;  // Chunks in each direction

    ChunkedTerrain();
    ~ChunkedTerrain();

    void init(unsigned int seed = 42);
    void update(float cameraX, float cameraZ);
    void render(GLuint program, const Mat4& mvp, const Mat4& model,
                const Vec3& lightDir, const Vec3& cameraPos, float time);

    float getHeightAt(float worldX, float worldZ) const;

private:
    PerlinNoise noise;
    std::unordered_map<std::pair<int, int>, TerrainChunk*, ChunkCoordHash> chunks;

    int lastCameraChunkX = INT_MAX;
    int lastCameraChunkZ = INT_MAX;

    void loadChunk(int chunkX, int chunkZ);
    void unloadDistantChunks(int centerX, int centerZ);

    static std::pair<int, int> worldToChunk(float worldX, float worldZ);
};
