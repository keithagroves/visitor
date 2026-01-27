#pragma once

#include <GLES3/gl3.h>

class Crosshair {
public:
    Crosshair();
    ~Crosshair();

    void generate(float aspectRatio);
    void render(GLuint program);

private:
    GLuint vao = 0;
    GLuint vbo = 0;
};
