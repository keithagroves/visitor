#pragma once

#include <cmath>
using std::sqrtf;
using std::tanf;
using std::sinf;
using std::cosf;
using std::floorf;
using std::powf;

struct Vec3 {
    float x, y, z;

    Vec3(float x = 0, float y = 0, float z = 0) : x(x), y(y), z(z) {}

    Vec3 operator+(const Vec3& v) const { return Vec3(x + v.x, y + v.y, z + v.z); }
    Vec3 operator-(const Vec3& v) const { return Vec3(x - v.x, y - v.y, z - v.z); }
    Vec3 operator*(float s) const { return Vec3(x * s, y * s, z * s); }
    Vec3 operator-() const { return Vec3(-x, -y, -z); }
    Vec3& operator+=(const Vec3& v) { x += v.x; y += v.y; z += v.z; return *this; }

    float length() const { return sqrtf(x*x + y*y + z*z); }

    Vec3 normalize() const {
        float len = length();
        return len > 0 ? Vec3(x/len, y/len, z/len) : Vec3();
    }

    Vec3 cross(const Vec3& v) const {
        return Vec3(y*v.z - z*v.y, z*v.x - x*v.z, x*v.y - y*v.x);
    }

    float dot(const Vec3& v) const { return x*v.x + y*v.y + z*v.z; }
};

// 4x4 matrix (column-major for OpenGL)
struct Mat4 {
    float m[16];

    Mat4() { identity(); }

    void identity() {
        for (int i = 0; i < 16; i++) m[i] = 0;
        m[0] = m[5] = m[10] = m[15] = 1;
    }

    static Mat4 perspective(float fov, float aspect, float near, float far) {
        Mat4 mat;
        float tanHalf = tanf(fov / 2.0f);
        for (int i = 0; i < 16; i++) mat.m[i] = 0;
        mat.m[0] = 1.0f / (aspect * tanHalf);
        mat.m[5] = 1.0f / tanHalf;
        mat.m[10] = -(far + near) / (far - near);
        mat.m[11] = -1.0f;
        mat.m[14] = -(2.0f * far * near) / (far - near);
        return mat;
    }

    static Mat4 lookAt(Vec3 eye, Vec3 center, Vec3 up) {
        Vec3 f = (center - eye).normalize();
        Vec3 s = f.cross(up).normalize();
        Vec3 u = s.cross(f);

        Mat4 mat;
        mat.m[0] = s.x;  mat.m[4] = s.y;  mat.m[8]  = s.z;  mat.m[12] = -s.dot(eye);
        mat.m[1] = u.x;  mat.m[5] = u.y;  mat.m[9]  = u.z;  mat.m[13] = -u.dot(eye);
        mat.m[2] = -f.x; mat.m[6] = -f.y; mat.m[10] = -f.z; mat.m[14] = f.dot(eye);
        mat.m[3] = 0;    mat.m[7] = 0;    mat.m[11] = 0;    mat.m[15] = 1;
        return mat;
    }

    Mat4 operator*(const Mat4& b) const {
        Mat4 result;
        for (int i = 0; i < 16; i++) result.m[i] = 0;
        for (int col = 0; col < 4; col++) {
            for (int row = 0; row < 4; row++) {
                for (int k = 0; k < 4; k++) {
                    result.m[col * 4 + row] += m[k * 4 + row] * b.m[col * 4 + k];
                }
            }
        }
        return result;
    }
};
