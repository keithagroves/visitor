#include "character.h"
#include <vector>
#include <cmath>

Character::Character() {}

Character::~Character() {
    if (vao) glDeleteVertexArrays(1, &vao);
    if (vbo) glDeleteBuffers(1, &vbo);
    if (ebo) glDeleteBuffers(1, &ebo);
}

void Character::addCylinder(std::vector<float>& verts, std::vector<unsigned int>& indices,
                            float y0, float y1, float radius, int segments, Vec3 color) {
    unsigned int baseIndex = verts.size() / 9;

    for (int i = 0; i <= segments; i++) {
        float angle = (float)i / segments * 2.0f * 3.14159f;
        float x = cosf(angle) * radius;
        float z = sinf(angle) * radius;
        Vec3 normal(cosf(angle), 0.0f, sinf(angle));

        // Bottom vertex
        verts.push_back(x);
        verts.push_back(y0);
        verts.push_back(z);
        verts.push_back(normal.x);
        verts.push_back(normal.y);
        verts.push_back(normal.z);
        verts.push_back(color.x);
        verts.push_back(color.y);
        verts.push_back(color.z);

        // Top vertex
        verts.push_back(x);
        verts.push_back(y1);
        verts.push_back(z);
        verts.push_back(normal.x);
        verts.push_back(normal.y);
        verts.push_back(normal.z);
        verts.push_back(color.x);
        verts.push_back(color.y);
        verts.push_back(color.z);
    }

    for (int i = 0; i < segments; i++) {
        unsigned int bl = baseIndex + i * 2;
        unsigned int br = baseIndex + i * 2 + 2;
        unsigned int tl = baseIndex + i * 2 + 1;
        unsigned int tr = baseIndex + i * 2 + 3;

        indices.push_back(bl);
        indices.push_back(br);
        indices.push_back(tl);
        indices.push_back(tl);
        indices.push_back(br);
        indices.push_back(tr);
    }
}

void Character::addSphere(std::vector<float>& verts, std::vector<unsigned int>& indices,
                          float cy, float radius, int segments, int rings, Vec3 color) {
    unsigned int baseIndex = verts.size() / 9;

    for (int r = 0; r <= rings; r++) {
        float phi = (float)r / rings * 3.14159f;
        float y = cy + cosf(phi) * radius;
        float ringRadius = sinf(phi) * radius;

        for (int s = 0; s <= segments; s++) {
            float theta = (float)s / segments * 2.0f * 3.14159f;
            float x = cosf(theta) * ringRadius;
            float z = sinf(theta) * ringRadius;

            Vec3 normal(x, y - cy, z);
            normal = normal.normalize();

            verts.push_back(x);
            verts.push_back(y);
            verts.push_back(z);
            verts.push_back(normal.x);
            verts.push_back(normal.y);
            verts.push_back(normal.z);
            verts.push_back(color.x);
            verts.push_back(color.y);
            verts.push_back(color.z);
        }
    }

    for (int r = 0; r < rings; r++) {
        for (int s = 0; s < segments; s++) {
            unsigned int curr = baseIndex + r * (segments + 1) + s;
            unsigned int next = curr + segments + 1;

            indices.push_back(curr);
            indices.push_back(next);
            indices.push_back(curr + 1);
            indices.push_back(curr + 1);
            indices.push_back(next);
            indices.push_back(next + 1);
        }
    }
}

void Character::generate() {
    std::vector<float> vertices;
    std::vector<unsigned int> indices;

    int segments = 16;
    int rings = 8;

    // Body (torso) - blue
    Vec3 bodyColor(0.2f, 0.4f, 0.8f);
    addCylinder(vertices, indices, 0.5f, 1.3f, RADIUS, segments, bodyColor);

    // Head - skin tone
    Vec3 headColor(0.9f, 0.75f, 0.65f);
    addSphere(vertices, indices, 1.5f, 0.2f, segments, rings, headColor);

    // Legs - darker blue
    Vec3 legColor(0.15f, 0.25f, 0.5f);
    addCylinder(vertices, indices, 0.0f, 0.5f, RADIUS * 0.4f, segments, legColor);

    // Left leg offset
    unsigned int legBase = vertices.size() / 9;
    addCylinder(vertices, indices, 0.0f, 0.5f, RADIUS * 0.4f, segments, legColor);
    // Offset left leg vertices
    for (unsigned int i = legBase; i < vertices.size() / 9; i++) {
        vertices[i * 9] -= 0.15f;  // x offset
    }

    // Right leg offset
    legBase = vertices.size() / 9;
    addCylinder(vertices, indices, 0.0f, 0.5f, RADIUS * 0.4f, segments, legColor);
    for (unsigned int i = legBase; i < vertices.size() / 9; i++) {
        vertices[i * 9] += 0.15f;  // x offset
    }

    // Arms - body color
    // Left arm
    unsigned int armBase = vertices.size() / 9;
    addCylinder(vertices, indices, 0.9f, 1.3f, RADIUS * 0.25f, segments, bodyColor);
    for (unsigned int i = armBase; i < vertices.size() / 9; i++) {
        vertices[i * 9] -= RADIUS + 0.1f;
    }

    // Right arm
    armBase = vertices.size() / 9;
    addCylinder(vertices, indices, 0.9f, 1.3f, RADIUS * 0.25f, segments, bodyColor);
    for (unsigned int i = armBase; i < vertices.size() / 9; i++) {
        vertices[i * 9] += RADIUS + 0.1f;
    }

    indexCount = indices.size();

    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ebo);

    glBindVertexArray(vao);

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);

    // Position
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    // Normal
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    // Color
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);
}

void Character::render() {
    glBindVertexArray(vao);
    glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, 0);
}
