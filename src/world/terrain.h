#pragma once

#include <GLES3/gl3.h>
#include "../math/vecmath.h"

class Terrain {
public:
    static constexpr int GRID_SIZE = 128;
    static constexpr float SCALE = 100.0f;
    static constexpr float HEIGHT_SCALE = 15.0f;

    Terrain();
    ~Terrain();

    void generate(unsigned int seed = 42);
    void render(GLuint program, const Mat4& mvp, const Mat4& model,
                const Vec3& lightDir, const Vec3& cameraPos, float time);

    float getHeightAt(float worldX, float worldZ) const;

private:
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint ebo = 0;
    int indexCount = 0;

    float heightMap[GRID_SIZE + 1][GRID_SIZE + 1];
};
