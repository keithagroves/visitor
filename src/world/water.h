#pragma once

#include <GLES3/gl3.h>
#include "../math/vecmath.h"

class Water {
public:
    Water();
    ~Water();

    void generate(float size);
    void render(GLuint program, const Mat4& mvp, const Vec3& cameraPos, float time);
    void renderDirect();  // Just draw, uniforms set externally

private:
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint ebo = 0;
    int indexCount = 0;
};
