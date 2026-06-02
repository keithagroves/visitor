#include "renderer2d.h"
#include "shader.h"

namespace {
const char* kVertexSrc = R"(#version 300 es
layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec4 a_color;
out vec4 v_color;
void main() {
    v_color = a_color;
    gl_Position = vec4(a_pos, 0.0, 1.0);
}
)";

const char* kFragmentSrc = R"(#version 300 es
precision mediump float;
in vec4 v_color;
out vec4 fragColor;
void main() {
    fragColor = v_color;
}
)";

constexpr int kFloatsPerVertex = 6; // clipX, clipY, r, g, b, a
}

void Renderer2D::init() {
    program_ = Shader::createProgram(kVertexSrc, kFragmentSrc);

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    // Start with a generous buffer; reallocated as the batch grows.
    glBufferData(GL_ARRAY_BUFFER, 1 << 16, nullptr, GL_DYNAMIC_DRAW);

    GLsizei stride = kFloatsPerVertex * sizeof(float);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, stride, (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    batch_.reserve(1 << 14);
}

void Renderer2D::begin(const Mat3& view, int screenW, int screenH) {
    view_ = view;
    screenW_ = screenW;
    screenH_ = screenH;
    stack_.clear();
    model_.identity();
    screenSpace_ = false;
    batch_.clear();
    blend_ = Blend::Normal;
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void Renderer2D::end() {
    flush();
}

void Renderer2D::pushMatrix() { stack_.push_back(model_); }

void Renderer2D::popMatrix() {
    if (!stack_.empty()) {
        model_ = stack_.back();
        stack_.pop_back();
    }
}

void Renderer2D::translate(float x, float y) { model_ = model_ * Mat3::translation(x, y); }
void Renderer2D::rotate(float deg)           { model_ = model_ * Mat3::rotation(deg); }
void Renderer2D::scale(float s)              { model_ = model_ * Mat3::scaling(s, s); }
void Renderer2D::scale(float sx, float sy)   { model_ = model_ * Mat3::scaling(sx, sy); }
void Renderer2D::resetMatrix()               { model_.identity(); }

void Renderer2D::fill(float r, float g, float b, float a) {
    r_ = r; g_ = g; b_ = b; a_ = a;
}

void Renderer2D::setBlend(Blend mode) {
    if (mode == blend_) return;
    flush();
    blend_ = mode;
    if (mode == Blend::Additive) {
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    } else {
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
}

void Renderer2D::beginScreenSpace() { screenSpace_ = true; }
void Renderer2D::endScreenSpace()   { screenSpace_ = false; }

void Renderer2D::addClip(float cx, float cy, float r, float g, float b, float a) {
    batch_.push_back(cx);
    batch_.push_back(cy);
    batch_.push_back(r);
    batch_.push_back(g);
    batch_.push_back(b);
    batch_.push_back(a);
}

void Renderer2D::addVertex(float x, float y) {
    addVertexColored(x, y, Color{r_, g_, b_, a_});
}

void Renderer2D::addVertexColored(float x, float y, const Color& c) {
    if (screenSpace_) {
        addClip(x, y, c.r, c.g, c.b, c.a);
        return;
    }
    Vec2 world = model_.transform(x, y);
    Vec2 clip = view_.transform(world.x, world.y);
    addClip(clip.x, clip.y, c.r, c.g, c.b, c.a);
}

void Renderer2D::triangleV(Vec2 a, Color ca, Vec2 b, Color cb, Vec2 c, Color cc) {
    addVertexColored(a.x, a.y, ca);
    addVertexColored(b.x, b.y, cb);
    addVertexColored(c.x, c.y, cc);
}

void Renderer2D::triangle(float x1, float y1, float x2, float y2, float x3, float y3) {
    addVertex(x1, y1);
    addVertex(x2, y2);
    addVertex(x3, y3);
}

void Renderer2D::quad(float x1, float y1, float x2, float y2,
                      float x3, float y3, float x4, float y4) {
    triangle(x1, y1, x2, y2, x3, y3);
    triangle(x1, y1, x3, y3, x4, y4);
}

void Renderer2D::ellipse(float cx, float cy, float rx, float ry, int segments) {
    if (segments < 3) segments = 3;
    float prevX = cx + rx;
    float prevY = cy;
    for (int i = 1; i <= segments; i++) {
        float a = (360.0f * i) / segments;
        float px = cx + rx * cosd(a);
        float py = cy + ry * sind(a);
        triangle(cx, cy, prevX, prevY, px, py);
        prevX = px;
        prevY = py;
    }
}

void Renderer2D::circle(float cx, float cy, float radius, int segments) {
    ellipse(cx, cy, radius, radius, segments);
}

void Renderer2D::line(float x1, float y1, float x2, float y2, float width) {
    float dx = x2 - x1, dy = y2 - y1;
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 1e-6f) return;
    float nx = -dy / len * (width * 0.5f);
    float ny = dx / len * (width * 0.5f);
    quad(x1 + nx, y1 + ny, x2 + nx, y2 + ny, x2 - nx, y2 - ny, x1 - nx, y1 - ny);
}

void Renderer2D::fanPolygon(float hubX, float hubY, const Vec2* pts, int count, bool closed) {
    if (count < 2) return;
    int last = closed ? count : count - 1;
    for (int i = 0; i < last; i++) {
        const Vec2& a = pts[i];
        const Vec2& b = pts[(i + 1) % count];
        triangle(hubX, hubY, a.x, a.y, b.x, b.y);
    }
}

void Renderer2D::convexPolygon(const Vec2* pts, int count) {
    if (count < 3) return;
    for (int i = 1; i < count - 1; i++) {
        triangle(pts[0].x, pts[0].y, pts[i].x, pts[i].y, pts[i + 1].x, pts[i + 1].y);
    }
}

void Renderer2D::flush() {
    if (batch_.empty()) return;

    glUseProgram(program_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    GLsizeiptr bytes = (GLsizeiptr)(batch_.size() * sizeof(float));
    glBufferData(GL_ARRAY_BUFFER, bytes, batch_.data(), GL_DYNAMIC_DRAW);

    GLsizei vertexCount = (GLsizei)(batch_.size() / kFloatsPerVertex);
    glDrawArrays(GL_TRIANGLES, 0, vertexCount);

    batch_.clear();
}
