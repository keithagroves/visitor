#pragma once

#include <cmath>
using std::sqrtf;
using std::tanf;
using std::sinf;
using std::cosf;
using std::floorf;
using std::powf;
using std::atan2f;
using std::acosf;

// Degrees-based trig so the port can keep the original p5.js angle conventions
// (angleMode(DEGREES)) one-to-one. The world is a 2D plane with +y pointing
// DOWN on screen, exactly like p5: rotate(deg) then reads as clockwise.
static constexpr float DEG2RAD = 3.14159265358979323846f / 180.0f;
static constexpr float RAD2DEG = 180.0f / 3.14159265358979323846f;
inline float sind(float deg) { return sinf(deg * DEG2RAD); }
inline float cosd(float deg) { return cosf(deg * DEG2RAD); }
inline float atan2d(float y, float x) { return atan2f(y, x) * RAD2DEG; }
inline float acosd(float c) {
    if (c < -1.0f) c = -1.0f;
    if (c > 1.0f) c = 1.0f;
    return acosf(c) * RAD2DEG;
}

struct Vec2 {
    float x, y;

    Vec2(float x = 0, float y = 0) : x(x), y(y) {}

    Vec2 operator+(const Vec2& v) const { return Vec2(x + v.x, y + v.y); }
    Vec2 operator-(const Vec2& v) const { return Vec2(x - v.x, y - v.y); }
    Vec2 operator*(float s) const { return Vec2(x * s, y * s); }
    Vec2 operator-() const { return Vec2(-x, -y); }
    Vec2& operator+=(const Vec2& v) { x += v.x; y += v.y; return *this; }
    Vec2& operator-=(const Vec2& v) { x -= v.x; y -= v.y; return *this; }
    Vec2& operator*=(float s) { x *= s; y *= s; return *this; }

    float length() const { return sqrtf(x * x + y * y); }
    float lengthSq() const { return x * x + y * y; }

    Vec2 normalize() const {
        float len = length();
        return len > 0 ? Vec2(x / len, y / len) : Vec2();
    }

    float dot(const Vec2& v) const { return x * v.x + y * v.y; }
};

inline float dist(float ax, float ay, float bx, float by) {
    float dx = bx - ax, dy = by - ay;
    return sqrtf(dx * dx + dy * dy);
}

// 3x3 affine matrix for 2D transforms (row-major). Bottom row is implicitly
// (0,0,1). Point transform applies p' = M * [x, y, 1]. Composition follows the
// p5/canvas convention: current = current * op, so the most-recently-pushed
// transform applies first to a vertex (innermost local space).
struct Mat3 {
    float m[9];

    Mat3() { identity(); }

    void identity() {
        m[0] = 1; m[1] = 0; m[2] = 0;
        m[3] = 0; m[4] = 1; m[5] = 0;
        m[6] = 0; m[7] = 0; m[8] = 1;
    }

    static Mat3 translation(float tx, float ty) {
        Mat3 r;
        r.m[2] = tx;
        r.m[5] = ty;
        return r;
    }

    static Mat3 scaling(float sx, float sy) {
        Mat3 r;
        r.m[0] = sx;
        r.m[4] = sy;
        return r;
    }

    // Rotation by degrees (clockwise on screen because +y is down).
    static Mat3 rotation(float deg) {
        float c = cosd(deg), s = sind(deg);
        Mat3 r;
        r.m[0] = c; r.m[1] = -s;
        r.m[3] = s; r.m[4] = c;
        return r;
    }

    Mat3 operator*(const Mat3& b) const {
        Mat3 r;
        for (int row = 0; row < 3; row++) {
            for (int col = 0; col < 3; col++) {
                r.m[row * 3 + col] =
                    m[row * 3 + 0] * b.m[0 * 3 + col] +
                    m[row * 3 + 1] * b.m[1 * 3 + col] +
                    m[row * 3 + 2] * b.m[2 * 3 + col];
            }
        }
        return r;
    }

    Vec2 transform(float x, float y) const {
        return Vec2(
            m[0] * x + m[1] * y + m[2],
            m[3] * x + m[4] * y + m[5]
        );
    }
    Vec2 transform(const Vec2& p) const { return transform(p.x, p.y); }
};

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
