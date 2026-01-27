#pragma once

#include <GLES3/gl3.h>
#include <vector>
#include "../math/vecmath.h"

// Simple low-poly house with walls and roof
class House {
public:
    House();
    ~House();

    void generate();
    void render();

    int getIndexCount() const { return indexCount; }
    GLuint getVao() const { return vao; }

private:
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint ebo = 0;
    int indexCount = 0;

    void addWalls(std::vector<float>& verts, std::vector<unsigned int>& indices,
                  float width, float height, float depth);
    void addRoof(std::vector<float>& verts, std::vector<unsigned int>& indices,
                 float width, float height, float depth, float roofHeight);
};

// Manages house instances across terrain
class HouseManager {
public:
    HouseManager();
    ~HouseManager();

    void init(unsigned int seed);
    void updateForChunk(int chunkX, int chunkZ, const float* heightData, int resolution, float chunkSize);
    void render(GLuint program, const Mat4& viewProjection, const Vec3& lightDir,
                const Vec3& cameraPos, float time);

    void clearChunk(int chunkX, int chunkZ);

private:
    House houseMesh;

    struct HouseInstance {
        Vec3 position;
        float rotation;
        float scale;
    };

    struct ChunkHouses {
        std::vector<HouseInstance> houses;
        bool generated = false;
    };

    std::vector<std::pair<std::pair<int,int>, ChunkHouses>> chunkHouses;

    unsigned int seed = 42;

    float hash(float x, float z) const;

    ChunkHouses* getOrCreateChunk(int chunkX, int chunkZ);
};
