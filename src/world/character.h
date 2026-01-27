#pragma once

#include <GLES3/gl3.h>
#include <vector>
#include "../math/vecmath.h"

// Simple character made of primitives (capsule-like shape)
class Character {
public:
    Character();
    ~Character();

    void generate();
    void render();

    void setPosition(const Vec3& pos) { position = pos; }
    void setRotation(float yaw) { rotationY = yaw; }

    Vec3 getPosition() const { return position; }
    float getRotation() const { return rotationY; }

    // Character dimensions
    static constexpr float HEIGHT = 1.8f;
    static constexpr float RADIUS = 0.3f;

private:
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint ebo = 0;
    int indexCount = 0;

    Vec3 position;
    float rotationY = 0.0f;

    void addCylinder(std::vector<float>& verts, std::vector<unsigned int>& indices,
                     float y0, float y1, float radius, int segments, Vec3 color);
    void addSphere(std::vector<float>& verts, std::vector<unsigned int>& indices,
                   float cy, float radius, int segments, int rings, Vec3 color);
};
