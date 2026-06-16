#pragma once

#include <GLES3/gl3.h>
#include <vector>
#include "../math/vecmath.h"

// Immediate-mode 2D renderer that stands in for the p5.js drawing API the
// original game was written against. Geometry is transformed on the CPU (model
// stack * camera view) into clip space and batched into a single dynamic VBO;
// the batch flushes when the blend mode changes or at frame end. That keeps the
// shader trivial and lets the port read like the p5 source: set a fill color,
// push/translate/rotate/scale, and emit triangles/polygons/circles.
class Renderer2D {
public:
    enum class Blend { Normal, Additive };

    void init();

    // Begin a frame. `view` maps world coordinates straight to clip space
    // (-1..1), with +y pointing down on screen to match p5's coordinate system.
    void begin(const Mat3& view, int screenW, int screenH);
    void end();

    // ---- transform stack (mirrors p5 push/pop/translate/rotate/scale) ----
    void pushMatrix();
    void popMatrix();
    void translate(float x, float y);
    void rotate(float deg);
    void scale(float s);
    void scale(float sx, float sy);
    void resetMatrix();

    // ---- state ----
    void fill(float r, float g, float b, float a = 1.0f);
    void setBlend(Blend mode);

    // Per-vertex RGBA color, for gradient primitives.
    struct Color { float r, g, b, a; };
    struct SpriteFrame {
        float x, y, w, h;
    };

    GLuint createTextureRGBA(int width, int height, const unsigned char* pixels, bool nearest = true);

    // ---- primitives (current fill color, current transform) ----
    void triangle(float x1, float y1, float x2, float y2, float x3, float y3);

    // Triangle with an independent color at each vertex (GL interpolates across
    // the face). Lets shapes fade core->edge for soft glows.
    void triangleV(Vec2 a, Color ca, Vec2 b, Color cb, Vec2 c, Color cc);
    void quad(float x1, float y1, float x2, float y2,
              float x3, float y3, float x4, float y4);
    // Filled circle / ellipse centered at (cx,cy).
    void circle(float cx, float cy, float radius, int segments = 32);
    void ellipse(float cx, float cy, float rx, float ry, int segments = 32);
    // Thick line as a quad of the given total width.
    void line(float x1, float y1, float x2, float y2, float width);

    // Triangle-fan polygon from an explicit hub point through the point list.
    // Used for the planet silhouette (hub = planet center) and any star-shaped
    // outline. `count` points, optionally treating it as a closed loop.
    void fanPolygon(float hubX, float hubY, const Vec2* pts, int count, bool closed);

    // Convex polygon triangulated as a fan from pts[0]. Fine for small convex
    // hull pieces (ship body parts).
    void convexPolygon(const Vec2* pts, int count);

    // Textured sprite from an atlas. Source rect is in pixels; destination rect
    // uses the current transform just like the shape primitives.
    void sprite(GLuint texture, int textureW, int textureH, SpriteFrame src,
                float dstX, float dstY, float dstW, float dstH,
                Color tint = Color{1, 1, 1, 1}, bool flipX = false, bool flipY = false);

    // Screen-space helpers: draw directly in clip space (-1..1), bypassing the
    // camera view. Used for the static starfield / HUD-ish overlays. Calls emit
    // clip coords into the same batch, so no flush is needed on the switch.
    void beginScreenSpace();
    void endScreenSpace();

private:
    void flush();
    void flushSprites();
    void addVertex(float x, float y);              // applies model+view, current color
    void addVertexColored(float x, float y, const Color& c); // model+view, explicit color
    void addClip(float cx, float cy, float r, float g, float b, float a);
    void addSpriteVertex(float x, float y, float u, float v, const Color& c);

    GLuint program_ = 0;
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLuint spriteProgram_ = 0;
    GLuint spriteVao_ = 0;
    GLuint spriteVbo_ = 0;
    GLuint spriteTexture_ = 0;

    std::vector<float> batch_;   // interleaved: clipX, clipY, r, g, b, a
    std::vector<float> spriteBatch_; // interleaved: clipX, clipY, u, v, r, g, b, a
    std::vector<Mat3> stack_;
    Mat3 model_;
    Mat3 view_;
    bool screenSpace_ = false;

    float r_ = 1, g_ = 1, b_ = 1, a_ = 1;
    Blend blend_ = Blend::Normal;
    int screenW_ = 1, screenH_ = 1;
};
