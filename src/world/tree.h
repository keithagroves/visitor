#pragma once

#include <GLES3/gl3.h>
#include <vector>
#include "../math/vecmath.h"

// Simple low-poly tree with trunk and foliage
class Tree {
public:
    Tree();
    ~Tree();

    void generate();
    void render();

    // Get vertex/index counts for instancing info
    int getIndexCount() const { return indexCount; }
    GLuint getVao() const { return vao; }

private:
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint ebo = 0;
    int indexCount = 0;

    void addTrunk(std::vector<float>& verts, std::vector<unsigned int>& indices,
                  float height, float radius, int segments);
    void addFoliage(std::vector<float>& verts, std::vector<unsigned int>& indices,
                    float baseY, float height, float radius, int segments);
};

// Manages tree instances across terrain
class TreeManager {
public:
    TreeManager();
    ~TreeManager();

    void init(unsigned int seed);
    void updateForChunk(int chunkX, int chunkZ, const float* heightData, int resolution, float chunkSize);
    void render(GLuint program, const Mat4& viewProjection, const Vec3& lightDir,
                const Vec3& cameraPos, float time);

    // Clear trees for a chunk
    void clearChunk(int chunkX, int chunkZ);

private:
    Tree treeMesh;

    struct TreeInstance {
        Vec3 position;
        float rotation;
        float scale;
    };

    // Trees grouped by chunk
    struct ChunkTrees {
        std::vector<TreeInstance> trees;
        bool generated = false;
    };

    std::vector<std::pair<std::pair<int,int>, ChunkTrees>> chunkTrees;

    unsigned int seed = 42;

    // Simple hash for deterministic placement
    float hash(float x, float z) const;

    ChunkTrees* getOrCreateChunk(int chunkX, int chunkZ);
};
