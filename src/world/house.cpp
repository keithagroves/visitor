#include "house.h"
#include <cmath>

// ============================================================================
// House mesh
// ============================================================================

House::House() {}

House::~House() {
    if (vao) glDeleteVertexArrays(1, &vao);
    if (vbo) glDeleteBuffers(1, &vbo);
    if (ebo) glDeleteBuffers(1, &ebo);
}

void House::generate() {
    std::vector<float> vertices;
    std::vector<unsigned int> indices;

    // House dimensions
    float width = 3.0f;
    float height = 2.5f;
    float depth = 4.0f;
    float roofHeight = 1.5f;

    addWalls(vertices, indices, width, height, depth);
    addRoof(vertices, indices, width, height, depth, roofHeight);

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

void House::addWalls(std::vector<float>& verts, std::vector<unsigned int>& indices,
                     float width, float height, float depth) {
    unsigned int baseIndex = verts.size() / 9;

    float hw = width / 2.0f;
    float hd = depth / 2.0f;

    // Wall colors - slightly varied for visual interest
    Vec3 wallColor1(0.85f, 0.82f, 0.75f);  // Light beige
    Vec3 wallColor2(0.80f, 0.77f, 0.70f);  // Slightly darker

    // Front face (z = -hd)
    Vec3 frontNormal(0, 0, -1);
    // Bottom left
    verts.insert(verts.end(), {-hw, 0, -hd, frontNormal.x, frontNormal.y, frontNormal.z, wallColor1.x, wallColor1.y, wallColor1.z});
    // Bottom right
    verts.insert(verts.end(), {hw, 0, -hd, frontNormal.x, frontNormal.y, frontNormal.z, wallColor1.x, wallColor1.y, wallColor1.z});
    // Top right
    verts.insert(verts.end(), {hw, height, -hd, frontNormal.x, frontNormal.y, frontNormal.z, wallColor1.x, wallColor1.y, wallColor1.z});
    // Top left
    verts.insert(verts.end(), {-hw, height, -hd, frontNormal.x, frontNormal.y, frontNormal.z, wallColor1.x, wallColor1.y, wallColor1.z});

    indices.insert(indices.end(), {baseIndex, baseIndex+1, baseIndex+2, baseIndex, baseIndex+2, baseIndex+3});
    baseIndex += 4;

    // Back face (z = hd)
    Vec3 backNormal(0, 0, 1);
    verts.insert(verts.end(), {hw, 0, hd, backNormal.x, backNormal.y, backNormal.z, wallColor1.x, wallColor1.y, wallColor1.z});
    verts.insert(verts.end(), {-hw, 0, hd, backNormal.x, backNormal.y, backNormal.z, wallColor1.x, wallColor1.y, wallColor1.z});
    verts.insert(verts.end(), {-hw, height, hd, backNormal.x, backNormal.y, backNormal.z, wallColor1.x, wallColor1.y, wallColor1.z});
    verts.insert(verts.end(), {hw, height, hd, backNormal.x, backNormal.y, backNormal.z, wallColor1.x, wallColor1.y, wallColor1.z});

    indices.insert(indices.end(), {baseIndex, baseIndex+1, baseIndex+2, baseIndex, baseIndex+2, baseIndex+3});
    baseIndex += 4;

    // Left face (x = -hw)
    Vec3 leftNormal(-1, 0, 0);
    verts.insert(verts.end(), {-hw, 0, hd, leftNormal.x, leftNormal.y, leftNormal.z, wallColor2.x, wallColor2.y, wallColor2.z});
    verts.insert(verts.end(), {-hw, 0, -hd, leftNormal.x, leftNormal.y, leftNormal.z, wallColor2.x, wallColor2.y, wallColor2.z});
    verts.insert(verts.end(), {-hw, height, -hd, leftNormal.x, leftNormal.y, leftNormal.z, wallColor2.x, wallColor2.y, wallColor2.z});
    verts.insert(verts.end(), {-hw, height, hd, leftNormal.x, leftNormal.y, leftNormal.z, wallColor2.x, wallColor2.y, wallColor2.z});

    indices.insert(indices.end(), {baseIndex, baseIndex+1, baseIndex+2, baseIndex, baseIndex+2, baseIndex+3});
    baseIndex += 4;

    // Right face (x = hw)
    Vec3 rightNormal(1, 0, 0);
    verts.insert(verts.end(), {hw, 0, -hd, rightNormal.x, rightNormal.y, rightNormal.z, wallColor2.x, wallColor2.y, wallColor2.z});
    verts.insert(verts.end(), {hw, 0, hd, rightNormal.x, rightNormal.y, rightNormal.z, wallColor2.x, wallColor2.y, wallColor2.z});
    verts.insert(verts.end(), {hw, height, hd, rightNormal.x, rightNormal.y, rightNormal.z, wallColor2.x, wallColor2.y, wallColor2.z});
    verts.insert(verts.end(), {hw, height, -hd, rightNormal.x, rightNormal.y, rightNormal.z, wallColor2.x, wallColor2.y, wallColor2.z});

    indices.insert(indices.end(), {baseIndex, baseIndex+1, baseIndex+2, baseIndex, baseIndex+2, baseIndex+3});
}

void House::addRoof(std::vector<float>& verts, std::vector<unsigned int>& indices,
                    float width, float height, float depth, float roofHeight) {
    unsigned int baseIndex = verts.size() / 9;

    float hw = width / 2.0f + 0.3f;  // Roof overhang
    float hd = depth / 2.0f + 0.3f;
    float roofY = height;
    float peakY = height + roofHeight;

    // Roof color - terracotta/red-brown
    Vec3 roofColor1(0.7f, 0.35f, 0.25f);
    Vec3 roofColor2(0.6f, 0.30f, 0.20f);

    // Calculate roof slope normals
    float roofAngle = atanf(roofHeight / hw);
    float nx = sinf(roofAngle);
    float ny = cosf(roofAngle);

    // Front roof slope
    Vec3 frontRoofNormal(0, ny, -nx);
    verts.insert(verts.end(), {-hw, roofY, -hd, frontRoofNormal.x, frontRoofNormal.y, frontRoofNormal.z, roofColor1.x, roofColor1.y, roofColor1.z});
    verts.insert(verts.end(), {hw, roofY, -hd, frontRoofNormal.x, frontRoofNormal.y, frontRoofNormal.z, roofColor1.x, roofColor1.y, roofColor1.z});
    verts.insert(verts.end(), {hw, peakY, 0, frontRoofNormal.x, frontRoofNormal.y, frontRoofNormal.z, roofColor1.x, roofColor1.y, roofColor1.z});
    verts.insert(verts.end(), {-hw, peakY, 0, frontRoofNormal.x, frontRoofNormal.y, frontRoofNormal.z, roofColor1.x, roofColor1.y, roofColor1.z});

    indices.insert(indices.end(), {baseIndex, baseIndex+1, baseIndex+2, baseIndex, baseIndex+2, baseIndex+3});
    baseIndex += 4;

    // Back roof slope
    Vec3 backRoofNormal(0, ny, nx);
    verts.insert(verts.end(), {hw, roofY, hd, backRoofNormal.x, backRoofNormal.y, backRoofNormal.z, roofColor2.x, roofColor2.y, roofColor2.z});
    verts.insert(verts.end(), {-hw, roofY, hd, backRoofNormal.x, backRoofNormal.y, backRoofNormal.z, roofColor2.x, roofColor2.y, roofColor2.z});
    verts.insert(verts.end(), {-hw, peakY, 0, backRoofNormal.x, backRoofNormal.y, backRoofNormal.z, roofColor2.x, roofColor2.y, roofColor2.z});
    verts.insert(verts.end(), {hw, peakY, 0, backRoofNormal.x, backRoofNormal.y, backRoofNormal.z, roofColor2.x, roofColor2.y, roofColor2.z});

    indices.insert(indices.end(), {baseIndex, baseIndex+1, baseIndex+2, baseIndex, baseIndex+2, baseIndex+3});
    baseIndex += 4;

    // Gable ends (triangles on the sides)
    // Left gable
    Vec3 leftGableNormal(-1, 0, 0);
    Vec3 gableColor(0.75f, 0.72f, 0.65f);
    verts.insert(verts.end(), {-hw + 0.3f, roofY, -hd + 0.3f, leftGableNormal.x, leftGableNormal.y, leftGableNormal.z, gableColor.x, gableColor.y, gableColor.z});
    verts.insert(verts.end(), {-hw + 0.3f, roofY, hd - 0.3f, leftGableNormal.x, leftGableNormal.y, leftGableNormal.z, gableColor.x, gableColor.y, gableColor.z});
    verts.insert(verts.end(), {-hw + 0.3f, peakY, 0, leftGableNormal.x, leftGableNormal.y, leftGableNormal.z, gableColor.x, gableColor.y, gableColor.z});

    indices.insert(indices.end(), {baseIndex, baseIndex+1, baseIndex+2});
    baseIndex += 3;

    // Right gable
    Vec3 rightGableNormal(1, 0, 0);
    verts.insert(verts.end(), {hw - 0.3f, roofY, hd - 0.3f, rightGableNormal.x, rightGableNormal.y, rightGableNormal.z, gableColor.x, gableColor.y, gableColor.z});
    verts.insert(verts.end(), {hw - 0.3f, roofY, -hd + 0.3f, rightGableNormal.x, rightGableNormal.y, rightGableNormal.z, gableColor.x, gableColor.y, gableColor.z});
    verts.insert(verts.end(), {hw - 0.3f, peakY, 0, rightGableNormal.x, rightGableNormal.y, rightGableNormal.z, gableColor.x, gableColor.y, gableColor.z});

    indices.insert(indices.end(), {baseIndex, baseIndex+1, baseIndex+2});
}

void House::render() {
    glBindVertexArray(vao);
    glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, 0);
}

// ============================================================================
// House Manager
// ============================================================================

HouseManager::HouseManager() {}

HouseManager::~HouseManager() {}

void HouseManager::init(unsigned int s) {
    seed = s + 12345;  // Different seed from trees
    houseMesh.generate();
}

float HouseManager::hash(float x, float z) const {
    float n = sinf(x * 17.9898f + z * 43.233f + (float)seed) * 23758.5453f;
    return n - floorf(n);
}

HouseManager::ChunkHouses* HouseManager::getOrCreateChunk(int chunkX, int chunkZ) {
    auto key = std::make_pair(chunkX, chunkZ);
    for (auto& pair : chunkHouses) {
        if (pair.first == key) {
            return &pair.second;
        }
    }
    chunkHouses.push_back({key, ChunkHouses()});
    return &chunkHouses.back().second;
}

void HouseManager::clearChunk(int chunkX, int chunkZ) {
    auto key = std::make_pair(chunkX, chunkZ);
    for (auto it = chunkHouses.begin(); it != chunkHouses.end(); ++it) {
        if (it->first == key) {
            chunkHouses.erase(it);
            return;
        }
    }
}

void HouseManager::updateForChunk(int chunkX, int chunkZ, const float* heightData,
                                   int resolution, float chunkSize) {
    ChunkHouses* chunk = getOrCreateChunk(chunkX, chunkZ);

    if (chunk->generated) return;
    chunk->generated = true;

    float worldOffsetX = chunkX * chunkSize;
    float worldOffsetZ = chunkZ * chunkSize;

    // Houses are sparse - only a few per chunk
    int housesPerChunk = 8;
    float spacing = chunkSize / 6.0f;  // Keep houses well apart

    for (int i = 0; i < housesPerChunk * 5; i++) {
        if ((int)chunk->houses.size() >= housesPerChunk) break;

        // Deterministic position within chunk
        float localX = hash(chunkX * 200.0f + i, chunkZ * 200.0f + i * 0.5f) * chunkSize;
        float localZ = hash(chunkX * 200.0f + i * 0.3f, chunkZ * 200.0f + i) * chunkSize;

        float worldX = worldOffsetX + localX;
        float worldZ = worldOffsetZ + localZ;

        // Sample height from heightData
        int gridX = (int)(localX / chunkSize * (resolution - 1));
        int gridZ = (int)(localZ / chunkSize * (resolution - 1));
        gridX = gridX < 0 ? 0 : (gridX >= resolution ? resolution - 1 : gridX);
        gridZ = gridZ < 0 ? 0 : (gridZ >= resolution ? resolution - 1 : gridZ);

        float rawHeight = heightData[gridZ * resolution + gridX];
        float shaderHeight = rawHeight * 2.0f - 0.3f;

        // Only place houses in flat grassy areas
        if (shaderHeight < 0.20f || shaderHeight > 0.32f) continue;

        // Check slope - houses need flatter ground than trees
        int gx1 = gridX > 0 ? gridX - 1 : 0;
        int gx2 = gridX < resolution - 1 ? gridX + 1 : resolution - 1;
        int gz1 = gridZ > 0 ? gridZ - 1 : 0;
        int gz2 = gridZ < resolution - 1 ? gridZ + 1 : resolution - 1;

        float h1 = heightData[gridZ * resolution + gx1] * 2.0f - 0.3f;
        float h2 = heightData[gridZ * resolution + gx2] * 2.0f - 0.3f;
        float h3 = heightData[gz1 * resolution + gridX] * 2.0f - 0.3f;
        float h4 = heightData[gz2 * resolution + gridX] * 2.0f - 0.3f;

        // Skip if near water (rivers)
        float waterThreshold = 0.15f;
        if (h1 < waterThreshold || h2 < waterThreshold || h3 < waterThreshold || h4 < waterThreshold) continue;

        float slopeX = (h2 - h1) * resolution / chunkSize;
        float slopeZ = (h4 - h3) * resolution / chunkSize;
        float slope = sqrtf(slopeX * slopeX + slopeZ * slopeZ);

        // Houses need very flat ground
        if (slope > 0.4f) continue;

        // Check spacing from existing houses
        bool tooClose = false;
        for (const auto& house : chunk->houses) {
            float dx = house.position.x - worldX;
            float dz = house.position.z - worldZ;
            if (dx * dx + dz * dz < spacing * spacing) {
                tooClose = true;
                break;
            }
        }
        if (tooClose) continue;

        // Add house
        float worldY = rawHeight * 80.0f - 15.0f;

        HouseInstance house;
        house.position = Vec3(worldX, worldY, worldZ);
        // Houses face cardinal directions (0, 90, 180, 270 degrees)
        int facing = (int)(hash(worldX + 100, worldZ + 100) * 4.0f);
        house.rotation = facing * 3.14159f / 2.0f;
        house.scale = 0.8f + hash(worldZ + 50, worldX + 50) * 0.4f;

        chunk->houses.push_back(house);
    }
}

void HouseManager::render(GLuint program, const Mat4& viewProjection,
                          const Vec3& lightDir, const Vec3& cameraPos, float time) {
    glUseProgram(program);

    glUniform3f(glGetUniformLocation(program, "uLightDir"), lightDir.x, lightDir.y, lightDir.z);
    glUniform3f(glGetUniformLocation(program, "uCameraPos"), cameraPos.x, cameraPos.y, cameraPos.z);
    glUniform1f(glGetUniformLocation(program, "uTime"), time);

    GLint mvpLoc = glGetUniformLocation(program, "uMVP");
    GLint modelLoc = glGetUniformLocation(program, "uModel");

    for (const auto& chunkPair : chunkHouses) {
        for (const auto& house : chunkPair.second.houses) {
            // Frustum culling by distance
            float dx = house.position.x - cameraPos.x;
            float dz = house.position.z - cameraPos.z;
            float distSq = dx * dx + dz * dz;
            if (distSq > 600.0f * 600.0f) continue;

            // Build model matrix
            Mat4 model;
            model.identity();

            // Scale
            model.m[0] = house.scale;
            model.m[5] = house.scale;
            model.m[10] = house.scale;

            // Rotate around Y
            float cosR = cosf(house.rotation);
            float sinR = sinf(house.rotation);
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
            sr.m[12] = house.position.x;
            sr.m[13] = house.position.y;
            sr.m[14] = house.position.z;

            Mat4 mvp = viewProjection * sr;

            glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, mvp.m);
            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, sr.m);

            houseMesh.render();
        }
    }
}
