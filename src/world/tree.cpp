#include "tree.h"
#include <cmath>

// ============================================================================
// Tree mesh
// ============================================================================

Tree::Tree() {}

Tree::~Tree() {
    if (vao) glDeleteVertexArrays(1, &vao);
    if (vbo) glDeleteBuffers(1, &vbo);
    if (ebo) glDeleteBuffers(1, &ebo);
}

void Tree::generate() {
    std::vector<float> vertices;
    std::vector<unsigned int> indices;

    // Create trunk
    addTrunk(vertices, indices, 2.0f, 0.15f, 6);

    // Create foliage layers (cone shapes stacked)
    addFoliage(vertices, indices, 1.2f, 1.5f, 1.0f, 8);
    addFoliage(vertices, indices, 2.0f, 1.2f, 0.8f, 8);
    addFoliage(vertices, indices, 2.6f, 1.0f, 0.6f, 8);

    indexCount = indices.size();

    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ebo);

    glBindVertexArray(vao);

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);

    // Position (location 0)
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    // Normal (location 1)
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    // Color (location 2)
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);

    glBindVertexArray(0);
}

void Tree::addTrunk(std::vector<float>& verts, std::vector<unsigned int>& indices,
                    float height, float radius, int segments) {
    unsigned int baseIndex = verts.size() / 9;

    Vec3 trunkColor(0.4f, 0.25f, 0.1f);  // Brown

    // Bottom ring
    for (int i = 0; i < segments; i++) {
        float angle = (float)i / segments * 2.0f * 3.14159f;
        float x = cosf(angle) * radius;
        float z = sinf(angle) * radius;
        Vec3 normal(cosf(angle), 0.0f, sinf(angle));

        verts.push_back(x);
        verts.push_back(0.0f);
        verts.push_back(z);
        verts.push_back(normal.x);
        verts.push_back(normal.y);
        verts.push_back(normal.z);
        verts.push_back(trunkColor.x);
        verts.push_back(trunkColor.y);
        verts.push_back(trunkColor.z);
    }

    // Top ring (slightly narrower)
    float topRadius = radius * 0.7f;
    for (int i = 0; i < segments; i++) {
        float angle = (float)i / segments * 2.0f * 3.14159f;
        float x = cosf(angle) * topRadius;
        float z = sinf(angle) * topRadius;
        Vec3 normal(cosf(angle), 0.0f, sinf(angle));

        verts.push_back(x);
        verts.push_back(height);
        verts.push_back(z);
        verts.push_back(normal.x);
        verts.push_back(normal.y);
        verts.push_back(normal.z);
        verts.push_back(trunkColor.x);
        verts.push_back(trunkColor.y);
        verts.push_back(trunkColor.z);
    }

    // Indices for trunk sides
    for (int i = 0; i < segments; i++) {
        int next = (i + 1) % segments;
        // Triangle 1
        indices.push_back(baseIndex + i);
        indices.push_back(baseIndex + next);
        indices.push_back(baseIndex + segments + i);
        // Triangle 2
        indices.push_back(baseIndex + next);
        indices.push_back(baseIndex + segments + next);
        indices.push_back(baseIndex + segments + i);
    }
}

void Tree::addFoliage(std::vector<float>& verts, std::vector<unsigned int>& indices,
                      float baseY, float height, float radius, int segments) {
    unsigned int baseIndex = verts.size() / 9;

    Vec3 leafColor(0.15f, 0.45f, 0.15f);  // Green
    Vec3 leafColorDark(0.1f, 0.35f, 0.1f);  // Darker green for variation

    // Cone tip
    verts.push_back(0.0f);
    verts.push_back(baseY + height);
    verts.push_back(0.0f);
    verts.push_back(0.0f);
    verts.push_back(1.0f);
    verts.push_back(0.0f);
    verts.push_back(leafColor.x);
    verts.push_back(leafColor.y);
    verts.push_back(leafColor.z);

    // Base ring
    for (int i = 0; i < segments; i++) {
        float angle = (float)i / segments * 2.0f * 3.14159f;
        float x = cosf(angle) * radius;
        float z = sinf(angle) * radius;

        // Normal points outward and slightly up
        float nx = cosf(angle);
        float nz = sinf(angle);
        float ny = radius / height;
        float len = sqrtf(nx*nx + ny*ny + nz*nz);
        nx /= len; ny /= len; nz /= len;

        // Alternate colors for low-poly look
        Vec3 color = (i % 2 == 0) ? leafColor : leafColorDark;

        verts.push_back(x);
        verts.push_back(baseY);
        verts.push_back(z);
        verts.push_back(nx);
        verts.push_back(ny);
        verts.push_back(nz);
        verts.push_back(color.x);
        verts.push_back(color.y);
        verts.push_back(color.z);
    }

    // Indices for cone
    for (int i = 0; i < segments; i++) {
        int next = (i + 1) % segments;
        indices.push_back(baseIndex);  // Tip
        indices.push_back(baseIndex + 1 + next);
        indices.push_back(baseIndex + 1 + i);
    }

    // Bottom cap
    unsigned int capCenter = verts.size() / 9;
    verts.push_back(0.0f);
    verts.push_back(baseY);
    verts.push_back(0.0f);
    verts.push_back(0.0f);
    verts.push_back(-1.0f);
    verts.push_back(0.0f);
    verts.push_back(leafColorDark.x);
    verts.push_back(leafColorDark.y);
    verts.push_back(leafColorDark.z);

    for (int i = 0; i < segments; i++) {
        int next = (i + 1) % segments;
        indices.push_back(capCenter);
        indices.push_back(baseIndex + 1 + i);
        indices.push_back(baseIndex + 1 + next);
    }
}

void Tree::render() {
    glBindVertexArray(vao);
    glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, 0);
}

// ============================================================================
// Tree Manager
// ============================================================================

TreeManager::TreeManager() {}

TreeManager::~TreeManager() {}

void TreeManager::init(unsigned int s) {
    seed = s;
    treeMesh.generate();
}

float TreeManager::hash(float x, float z) const {
    // Simple deterministic hash
    float n = sinf(x * 12.9898f + z * 78.233f + (float)seed) * 43758.5453f;
    return n - floorf(n);
}

TreeManager::ChunkTrees* TreeManager::getOrCreateChunk(int chunkX, int chunkZ) {
    auto key = std::make_pair(chunkX, chunkZ);
    for (auto& pair : chunkTrees) {
        if (pair.first == key) {
            return &pair.second;
        }
    }
    chunkTrees.push_back({key, ChunkTrees()});
    return &chunkTrees.back().second;
}

void TreeManager::clearChunk(int chunkX, int chunkZ) {
    auto key = std::make_pair(chunkX, chunkZ);
    for (auto it = chunkTrees.begin(); it != chunkTrees.end(); ++it) {
        if (it->first == key) {
            chunkTrees.erase(it);
            return;
        }
    }
}

void TreeManager::updateForChunk(int chunkX, int chunkZ, const float* heightData,
                                  int resolution, float chunkSize) {
    ChunkTrees* chunk = getOrCreateChunk(chunkX, chunkZ);

    if (chunk->generated) return;
    chunk->generated = true;

    float worldOffsetX = chunkX * chunkSize;
    float worldOffsetZ = chunkZ * chunkSize;

    // Scatter trees using deterministic random
    int treesPerChunk = 900;  // Moderate forest density
    float spacing = chunkSize / 60.0f;  // Slightly more spread out

    for (int i = 0; i < treesPerChunk * 3; i++) {  // Try more times than needed
        if ((int)chunk->trees.size() >= treesPerChunk) break;

        // Deterministic position within chunk - use multiple hash variations for better distribution
        float localX = hash(chunkX * 100.0f + i, chunkZ * 100.0f + i * 0.3f) * chunkSize;
        float localZ = hash(chunkX * 100.0f + i * 0.7f, chunkZ * 100.0f + i) * chunkSize;

        float worldX = worldOffsetX + localX;
        float worldZ = worldOffsetZ + localZ;

        // Sample height from heightData
        int gridX = (int)(localX / chunkSize * (resolution - 1));
        int gridZ = (int)(localZ / chunkSize * (resolution - 1));
        gridX = gridX < 0 ? 0 : (gridX >= resolution ? resolution - 1 : gridX);
        gridZ = gridZ < 0 ? 0 : (gridZ >= resolution ? resolution - 1 : gridZ);

        float rawHeight = heightData[gridZ * resolution + gridX];

        // Convert raw height to shader height for grass zone check
        // Shader uses: vHeight = h * 2.0 - 0.3
        float shaderHeight = rawHeight * 2.0f - 0.3f;

        // Only place trees in GREEN GRASS areas!
        // Shader: h < 0.15 is sand/beach, h >= 0.15 && h < 0.4 is green grass
        if (shaderHeight < 0.18f || shaderHeight > 0.38f) continue;  // Grass zone with small margins

        // Check slope by sampling neighbors - also detect river banks
        int gx1 = gridX > 0 ? gridX - 1 : 0;
        int gx2 = gridX < resolution - 1 ? gridX + 1 : resolution - 1;
        int gz1 = gridZ > 0 ? gridZ - 1 : 0;
        int gz2 = gridZ < resolution - 1 ? gridZ + 1 : resolution - 1;

        float h1 = heightData[gridZ * resolution + gx1] * 2.0f - 0.3f;
        float h2 = heightData[gridZ * resolution + gx2] * 2.0f - 0.3f;
        float h3 = heightData[gz1 * resolution + gridX] * 2.0f - 0.3f;
        float h4 = heightData[gz2 * resolution + gridX] * 2.0f - 0.3f;

        // Skip if any neighboring cell is near water (river detection)
        float waterThreshold = 0.12f;
        if (h1 < waterThreshold || h2 < waterThreshold || h3 < waterThreshold || h4 < waterThreshold) continue;

        float slopeX = (h2 - h1) * resolution / chunkSize;
        float slopeZ = (h4 - h3) * resolution / chunkSize;
        float slope = sqrtf(slopeX * slopeX + slopeZ * slopeZ);

        // Skip steep slopes
        if (slope > 1.2f) continue;

        // Check minimum spacing from existing trees (sparse check for performance)
        bool tooClose = false;
        // Only check nearby trees for performance with high tree counts
        int checkLimit = chunk->trees.size() > 100 ? 50 : chunk->trees.size();
        for (int j = chunk->trees.size() - 1; j >= 0 && j >= (int)chunk->trees.size() - checkLimit; j--) {
            const auto& tree = chunk->trees[j];
            float dx = tree.position.x - worldX;
            float dz = tree.position.z - worldZ;
            if (dx * dx + dz * dz < spacing * spacing) {
                tooClose = true;
                break;
            }
        }
        if (tooClose) continue;

        // Add tree with more size variation
        // Convert RAW height to world Y position (same as terrain: rawH * HEIGHT_SCALE - TERRAIN_OFFSET)
        float worldY = rawHeight * 80.0f - 15.0f;  // Match ChunkedTerrain::HEIGHT_SCALE and TERRAIN_OFFSET

        TreeInstance tree;
        tree.position = Vec3(worldX, worldY, worldZ);
        tree.rotation = hash(worldX, worldZ) * 2.0f * 3.14159f;
        tree.scale = 0.5f + hash(worldZ, worldX) * 0.8f;  // 0.5 to 1.3 - more variation

        chunk->trees.push_back(tree);
    }
}

void TreeManager::render(GLuint program, const Mat4& viewProjection,
                         const Vec3& lightDir, const Vec3& cameraPos, float time) {
    glUseProgram(program);

    glUniform3f(glGetUniformLocation(program, "uLightDir"), lightDir.x, lightDir.y, lightDir.z);
    glUniform3f(glGetUniformLocation(program, "uCameraPos"), cameraPos.x, cameraPos.y, cameraPos.z);
    glUniform1f(glGetUniformLocation(program, "uTime"), time);

    GLint mvpLoc = glGetUniformLocation(program, "uMVP");
    GLint modelLoc = glGetUniformLocation(program, "uModel");

    // Render each tree instance
    for (const auto& chunkPair : chunkTrees) {
        for (const auto& tree : chunkPair.second.trees) {
            // Simple frustum culling by distance
            float dx = tree.position.x - cameraPos.x;
            float dz = tree.position.z - cameraPos.z;
            float distSq = dx * dx + dz * dz;
            if (distSq > 500.0f * 500.0f) continue;  // Skip distant trees

            // Build model matrix
            Mat4 model;
            model.identity();

            // Scale
            model.m[0] = tree.scale;
            model.m[5] = tree.scale;
            model.m[10] = tree.scale;

            // Rotate around Y
            float cosR = cosf(tree.rotation);
            float sinR = sinf(tree.rotation);
            Mat4 rot;
            rot.identity();
            rot.m[0] = cosR;
            rot.m[2] = -sinR;
            rot.m[8] = sinR;
            rot.m[10] = cosR;

            // Combine rotation with scale
            Mat4 sr;
            sr.identity();
            sr.m[0] = model.m[0] * rot.m[0];
            sr.m[2] = model.m[0] * rot.m[2];
            sr.m[5] = model.m[5];
            sr.m[8] = model.m[10] * rot.m[8];
            sr.m[10] = model.m[10] * rot.m[10];

            // Translate
            sr.m[12] = tree.position.x;
            sr.m[13] = tree.position.y;
            sr.m[14] = tree.position.z;

            Mat4 mvp = viewProjection * sr;

            glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, mvp.m);
            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, sr.m);

            treeMesh.render();
        }
    }
}
