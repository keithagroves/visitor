#include "terrain.h"
#include "../math/noise.h"
#include <vector>
#include <cstdio>

Terrain::Terrain() {}

Terrain::~Terrain() {
    if (vao) glDeleteVertexArrays(1, &vao);
    if (vbo) glDeleteBuffers(1, &vbo);
    if (ebo) glDeleteBuffers(1, &ebo);
}

void Terrain::generate(unsigned int seed) {
    PerlinNoise noise(seed);

    std::vector<float> vertices;
    std::vector<unsigned int> indices;

    // Generate heightmap
    for (int z = 0; z <= GRID_SIZE; z++) {
        for (int x = 0; x <= GRID_SIZE; x++) {
            float fx = (float)x / GRID_SIZE;
            float fz = (float)z / GRID_SIZE;

            float h = noise.octaveNoise(fx * 4, fz * 4, 6, 0.5f);
            h += noise.octaveNoise(fx * 8 + 100, fz * 8 + 100, 4, 0.5f) * 0.3f;
            h = (h + 1.0f) * 0.5f;
            h = powf(h, 1.3f);

            heightMap[z][x] = h;
        }
    }

    // Generate vertices with normals
    for (int z = 0; z <= GRID_SIZE; z++) {
        for (int x = 0; x <= GRID_SIZE; x++) {
            float fx = (float)x / GRID_SIZE - 0.5f;
            float fz = (float)z / GRID_SIZE - 0.5f;
            float h = heightMap[z][x];

            float px = fx * SCALE;
            float py = h * HEIGHT_SCALE - 2.0f;
            float pz = fz * SCALE;

            float hL = x > 0 ? heightMap[z][x-1] : h;
            float hR = x < GRID_SIZE ? heightMap[z][x+1] : h;
            float hD = z > 0 ? heightMap[z-1][x] : h;
            float hU = z < GRID_SIZE ? heightMap[z+1][x] : h;

            Vec3 normal = Vec3(hL - hR, 2.0f / GRID_SIZE * 10, hD - hU).normalize();

            vertices.push_back(px);
            vertices.push_back(py);
            vertices.push_back(pz);
            vertices.push_back(normal.x);
            vertices.push_back(normal.y);
            vertices.push_back(normal.z);
            vertices.push_back(h * 2.0f - 0.3f);
        }
    }

    // Generate indices
    for (int z = 0; z < GRID_SIZE; z++) {
        for (int x = 0; x < GRID_SIZE; x++) {
            int topLeft = z * (GRID_SIZE + 1) + x;
            int topRight = topLeft + 1;
            int bottomLeft = (z + 1) * (GRID_SIZE + 1) + x;
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

    // Position
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    // Normal
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    // Height
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);

    printf("Terrain generated: %d vertices, %d triangles\n",
           (int)vertices.size() / 7, indexCount / 3);
}

void Terrain::render(GLuint program, const Mat4& mvp, const Mat4& model,
                     const Vec3& lightDir, const Vec3& cameraPos, float time) {
    glUseProgram(program);
    glUniformMatrix4fv(glGetUniformLocation(program, "uMVP"), 1, GL_FALSE, mvp.m);
    glUniformMatrix4fv(glGetUniformLocation(program, "uModel"), 1, GL_FALSE, model.m);
    glUniform3f(glGetUniformLocation(program, "uLightDir"), lightDir.x, lightDir.y, lightDir.z);
    glUniform3f(glGetUniformLocation(program, "uCameraPos"), cameraPos.x, cameraPos.y, cameraPos.z);
    glUniform1f(glGetUniformLocation(program, "uTime"), time);

    glBindVertexArray(vao);
    glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, 0);
}

float Terrain::getHeightAt(float worldX, float worldZ) const {
    float gridX = (worldX / SCALE + 0.5f) * GRID_SIZE;
    float gridZ = (worldZ / SCALE + 0.5f) * GRID_SIZE;

    if (gridX < 0) gridX = 0;
    if (gridZ < 0) gridZ = 0;
    if (gridX >= GRID_SIZE) gridX = GRID_SIZE - 0.001f;
    if (gridZ >= GRID_SIZE) gridZ = GRID_SIZE - 0.001f;

    int x0 = (int)gridX;
    int z0 = (int)gridZ;
    int x1 = x0 + 1;
    int z1 = z0 + 1;

    if (x1 > GRID_SIZE) x1 = GRID_SIZE;
    if (z1 > GRID_SIZE) z1 = GRID_SIZE;

    float fx = gridX - x0;
    float fz = gridZ - z0;

    float h00 = heightMap[z0][x0];
    float h10 = heightMap[z0][x1];
    float h01 = heightMap[z1][x0];
    float h11 = heightMap[z1][x1];

    float h0 = h00 * (1 - fx) + h10 * fx;
    float h1 = h01 * (1 - fx) + h11 * fx;
    float h = h0 * (1 - fz) + h1 * fz;

    return h * HEIGHT_SCALE - 2.0f;
}
