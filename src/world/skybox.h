#pragma once

#include <GLES3/gl3.h>
#include "../math/vecmath.h"

class Skybox {
public:
    Skybox();
    ~Skybox();

    void generate();
    void render(GLuint program, const Mat4& viewProjection, const Vec3& sunDirection, float time);

private:
    GLuint vao = 0;
    GLuint vbo = 0;
};
