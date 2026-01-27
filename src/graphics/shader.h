#pragma once

#include <GLES3/gl3.h>

namespace Shader {
    GLuint compile(GLenum type, const char* source);
    GLuint createProgram(const char* vertSrc, const char* fragSrc);
}
