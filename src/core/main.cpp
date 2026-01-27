#include <emscripten.h>
#include <emscripten/html5.h>
#include <GLES3/gl3.h>
#include <cstdio>

#include "../math/vecmath.h"
#include "../graphics/shader.h"
#include "../graphics/shaders.h"
#include "../world/chunked_terrain.h"
#include "../world/water.h"
#include "../input/camera.h"
#include "../input/crosshair.h"
#include "../input/input.h"

// ============================================================================
// Application state
// ============================================================================

struct App {
    // Rendering
    GLuint terrainProgram = 0;
    GLuint waterProgram = 0;
    GLuint crosshairProgram = 0;
    GLuint underwaterProgram = 0;
    GLuint underwaterOverlayProgram = 0;

    // Game objects
    ChunkedTerrain terrain;
    Water water;
    Camera camera;
    Crosshair crosshair;

    // Underwater overlay quad
    GLuint overlayVao = 0;
    GLuint overlayVbo = 0;

    // Timing
    double startTime = 0;
    double lastFrameTime = 0;

    // Screen
    int width = 800;
    int height = 600;
    float aspectRatio = 800.0f / 600.0f;
    float fov = 70.0f * 3.14159f / 180.0f;
};

static App app;

// ============================================================================
// Game logic
// ============================================================================

void init() {
    // Create shader programs
    app.terrainProgram = Shader::createProgram(Shaders::terrainVertex, Shaders::terrainFragment);
    app.waterProgram = Shader::createProgram(Shaders::waterVertex, Shaders::waterFragment);
    app.crosshairProgram = Shader::createProgram(Shaders::crosshairVertex, Shaders::crosshairFragment);
    app.underwaterProgram = Shader::createProgram(Shaders::underwaterVertex, Shaders::underwaterFragment);
    app.underwaterOverlayProgram = Shader::createProgram(Shaders::underwaterOverlayVertex, Shaders::underwaterOverlayFragment);

    // Create fullscreen quad for underwater overlay
    float overlayQuad[] = {
        -1.0f, -1.0f,
         1.0f, -1.0f,
        -1.0f,  1.0f,
         1.0f,  1.0f
    };
    glGenVertexArrays(1, &app.overlayVao);
    glGenBuffers(1, &app.overlayVbo);
    glBindVertexArray(app.overlayVao);
    glBindBuffer(GL_ARRAY_BUFFER, app.overlayVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(overlayQuad), overlayQuad, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    // Initialize terrain system
    app.terrain.init(42);

    // Generate initial chunks around origin
    app.terrain.update(0, 0);

    // Water follows camera - large plane for big world
    app.water.generate(2000.0f);
    app.crosshair.generate(app.aspectRatio);

    // Position camera
    Vec3 startPos(0, 0, 0);
    startPos.y = app.terrain.getHeightAt(0, 0) + Camera::DEFAULT_HEIGHT;
    app.camera.setPosition(startPos);

    // Initialize input
    Input::init("#canvas");

    // OpenGL state
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glClearColor(0.7f, 0.8f, 0.95f, 1.0f);

    // Timing
    app.startTime = emscripten_get_now() / 1000.0;
    app.lastFrameTime = app.startTime;

    printf("=== Infinite Terrain Explorer ===\n");
    printf("Click canvas to enable mouse look\n");
    printf("WASD - Move | Mouse - Look | Space/Shift - Up/Down | ESC - Release\n");
}

void update(float deltaTime) {
    // Mouse look
    if (Input::isPointerLocked()) {
        app.camera.rotate(Input::getMouseDeltaX(), Input::getMouseDeltaY());
        Input::resetMouseDelta();
    }

    // Movement direction
    Vec3 moveDir(0, 0, 0);
    Vec3 forward = app.camera.getForward();
    Vec3 right = app.camera.getRight();

    if (Input::isKeyDown(Input::KEY_W) || Input::isKeyDown(Input::KEY_UP))
        moveDir += forward;
    if (Input::isKeyDown(Input::KEY_S) || Input::isKeyDown(Input::KEY_DOWN))
        moveDir += forward * -1.0f;
    if (Input::isKeyDown(Input::KEY_A) || Input::isKeyDown(Input::KEY_LEFT))
        moveDir += right * -1.0f;
    if (Input::isKeyDown(Input::KEY_D) || Input::isKeyDown(Input::KEY_RIGHT))
        moveDir += right;

    if (moveDir.length() > 0) {
        moveDir = moveDir.normalize();
        app.camera.move(moveDir, deltaTime);
    }

    // Vertical movement
    if (Input::isKeyDown(Input::KEY_SPACE))
        app.camera.moveVertical(1.0f, deltaTime);
    if (Input::isKeyDown(Input::KEY_SHIFT))
        app.camera.moveVertical(-1.0f, deltaTime);

    // Update terrain chunks based on camera position
    Vec3 pos = app.camera.getPosition();
    app.terrain.update(pos.x, pos.z);

    // Terrain collision (no bounds - infinite terrain!)
    float terrainHeight = app.terrain.getHeightAt(pos.x, pos.z);
    app.camera.clampToTerrain(terrainHeight);
}

void render(float time) {
    Vec3 cameraPos = app.camera.getPosition();
    bool isUnderwater = cameraPos.y < 0.0f;

    // Matrices
    Mat4 view = app.camera.getViewMatrix();
    Mat4 projection = Mat4::perspective(app.fov, app.aspectRatio, 0.5f, 2000.0f);
    Mat4 model;
    model.identity();
    Mat4 mvp = projection * view * model;

    // Water matrix - centered on camera
    Mat4 waterModel;
    waterModel.identity();
    // Translate water to follow camera (snapped to prevent swimming effect)
    waterModel.m[12] = floorf(cameraPos.x / 50.0f) * 50.0f;
    waterModel.m[14] = floorf(cameraPos.z / 50.0f) * 50.0f;
    Mat4 waterMvp = projection * view * waterModel;

    // Light direction
    Vec3 lightDir = Vec3(0.5f, 0.8f, 0.3f).normalize();

    // Clear - darker blue when underwater
    if (isUnderwater) {
        glClearColor(0.0f, 0.15f, 0.3f, 1.0f);
    } else {
        glClearColor(0.7f, 0.8f, 0.95f, 1.0f);
    }
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Draw terrain
    app.terrain.render(app.terrainProgram, mvp, model, lightDir, cameraPos, time);

    // Draw water surface
    if (isUnderwater) {
        // Underwater: render water surface from below (flip culling)
        glCullFace(GL_FRONT);
        glEnable(GL_CULL_FACE);
        glUseProgram(app.underwaterProgram);
        glUniformMatrix4fv(glGetUniformLocation(app.underwaterProgram, "uMVP"), 1, GL_FALSE, waterMvp.m);
        glUniform3f(glGetUniformLocation(app.underwaterProgram, "uCameraPos"), cameraPos.x, cameraPos.y, cameraPos.z);
        glUniform1f(glGetUniformLocation(app.underwaterProgram, "uTime"), time);
        app.water.renderDirect();
        glDisable(GL_CULL_FACE);
    } else {
        // Above water: normal water surface
        glUseProgram(app.waterProgram);
        glUniformMatrix4fv(glGetUniformLocation(app.waterProgram, "uMVP"), 1, GL_FALSE, waterMvp.m);
        glUniform3f(glGetUniformLocation(app.waterProgram, "uCameraPos"), cameraPos.x, cameraPos.y, cameraPos.z);
        glUniform1f(glGetUniformLocation(app.waterProgram, "uTime"), time);
        app.water.renderDirect();
    }

    // Underwater overlay effect
    if (isUnderwater) {
        glDisable(GL_DEPTH_TEST);
        glUseProgram(app.underwaterOverlayProgram);
        glUniform1f(glGetUniformLocation(app.underwaterOverlayProgram, "uTime"), time);
        glBindVertexArray(app.overlayVao);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glEnable(GL_DEPTH_TEST);
    }

    // Draw crosshair
    app.crosshair.render(app.crosshairProgram);
}

void updateViewport() {
    int w, h;
    emscripten_get_canvas_element_size("#canvas", &w, &h);

    if (w != app.width || h != app.height) {
        app.width = w;
        app.height = h;
        app.aspectRatio = (float)w / (float)h;
        glViewport(0, 0, w, h);

        // Regenerate crosshair with new aspect ratio
        app.crosshair.generate(app.aspectRatio);
    }
}

void mainLoop() {
    // Handle resize
    updateViewport();

    double currentTime = emscripten_get_now() / 1000.0;
    float time = static_cast<float>(currentTime - app.startTime);
    float deltaTime = static_cast<float>(currentTime - app.lastFrameTime);
    app.lastFrameTime = currentTime;

    // Clamp delta time
    if (deltaTime > 0.1f) deltaTime = 0.1f;

    update(deltaTime);
    render(time);
}

// ============================================================================
// Entry point
// ============================================================================

int main() {
    EmscriptenWebGLContextAttributes attrs;
    emscripten_webgl_init_context_attributes(&attrs);
    attrs.majorVersion = 2;
    attrs.minorVersion = 0;
    attrs.alpha = false;
    attrs.depth = true;
    attrs.stencil = false;
    attrs.antialias = true;

    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE ctx = emscripten_webgl_create_context("#canvas", &attrs);
    if (ctx <= 0) {
        printf("Failed to create WebGL 2 context!\n");
        return 1;
    }
    emscripten_webgl_make_context_current(ctx);

    // Get initial canvas size
    emscripten_get_canvas_element_size("#canvas", &app.width, &app.height);
    app.aspectRatio = (float)app.width / (float)app.height;
    glViewport(0, 0, app.width, app.height);

    init();
    emscripten_set_main_loop(mainLoop, 0, 1);

    return 0;
}
