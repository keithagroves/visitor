#include <emscripten.h>
#include <emscripten/html5.h>
#include <GLES3/gl3.h>
#include <cstdio>
#include <cmath>

#include "../math/vecmath.h"
#include "../graphics/shader.h"
#include "../graphics/shaders.h"
#include "../world/chunked_terrain.h"
#include "../world/water.h"
#include "../world/character.h"
#include "../world/skybox.h"
#include "../input/camera.h"
#include "../input/crosshair.h"
#include "../input/input.h"

// ============================================================================
// Application state
// ============================================================================

struct Player {
    Vec3 position;
    float rotation = 0.0f;  // Y-axis rotation (facing direction)
    float verticalVelocity = 0.0f;  // For jumping
    bool isGrounded = true;

    static constexpr float MOVE_SPEED = 6.0f;
    static constexpr float ROTATION_SPEED = 5.0f;
    static constexpr float JUMP_FORCE = 12.0f;
    static constexpr float GRAVITY = 25.0f;
};

struct App {
    // Rendering
    GLuint terrainProgram = 0;
    GLuint waterProgram = 0;
    GLuint crosshairProgram = 0;
    GLuint underwaterProgram = 0;
    GLuint underwaterOverlayProgram = 0;
    GLuint characterProgram = 0;
    GLuint skyboxProgram = 0;
    GLuint treeProgram = 0;
    GLuint debugLineProgram = 0;

    // Debug ray visualization
    GLuint debugLineVao = 0;
    GLuint debugLineVbo = 0;

    // Game objects
    ChunkedTerrain terrain;
    Water water;
    Camera camera;
    Crosshair crosshair;
    Character character;
    Skybox skybox;
    Player player;

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
    float fov = 60.0f * 3.14159f / 180.0f;  // Slightly narrower for third-person
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
    app.characterProgram = Shader::createProgram(Shaders::characterVertex, Shaders::characterFragment);
    app.skyboxProgram = Shader::createProgram(Shaders::skyboxVertex, Shaders::skyboxFragment);
    app.treeProgram = Shader::createProgram(Shaders::treeVertex, Shaders::treeFragment);
    app.debugLineProgram = Shader::createProgram(Shaders::debugLineVertex, Shaders::debugLineFragment);

    // Create debug line VAO/VBO (will update vertices each frame)
    glGenVertexArrays(1, &app.debugLineVao);
    glGenBuffers(1, &app.debugLineVbo);
    glBindVertexArray(app.debugLineVao);
    glBindBuffer(GL_ARRAY_BUFFER, app.debugLineVbo);
    glBufferData(GL_ARRAY_BUFFER, 6 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

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

    // Generate character mesh
    app.character.generate();

    // Generate skybox
    app.skybox.generate();

    // Position player
    app.player.position = Vec3(0, 0, 0);
    app.player.position.y = app.terrain.getHeightAt(0, 0);
    app.player.rotation = 0.0f;

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

    printf("=== Third Person Explorer ===\n");
    printf("Click canvas to enable controls\n");
    printf("WASD - Move | Mouse - Camera | Scroll - Zoom | ESC - Release\n");
}

void update(float deltaTime) {
    // Camera rotation with mouse
    if (Input::isPointerLocked()) {
        app.camera.rotate(Input::getMouseDeltaX(), Input::getMouseDeltaY());
        Input::resetMouseDelta();
    }

    // Get camera-relative directions for movement
    Vec3 camForward = app.camera.getForward();
    Vec3 camRight = app.camera.getRight();

    // Movement input
    Vec3 moveDir(0, 0, 0);
    bool isMoving = false;

    if (Input::isKeyDown(Input::KEY_W) || Input::isKeyDown(Input::KEY_UP)) {
        moveDir += camForward;
        isMoving = true;
    }
    if (Input::isKeyDown(Input::KEY_S) || Input::isKeyDown(Input::KEY_DOWN)) {
        moveDir += camForward * -1.0f;
        isMoving = true;
    }
    if (Input::isKeyDown(Input::KEY_A) || Input::isKeyDown(Input::KEY_LEFT)) {
        moveDir += camRight * -1.0f;
        isMoving = true;
    }
    if (Input::isKeyDown(Input::KEY_D) || Input::isKeyDown(Input::KEY_RIGHT)) {
        moveDir += camRight;
        isMoving = true;
    }

    // Move player
    if (isMoving && moveDir.length() > 0) {
        moveDir = moveDir.normalize();

        // Rotate character to face movement direction (add PI to face away from camera)
        float targetRotation = atan2f(moveDir.x, moveDir.z);
        float rotDiff = targetRotation - app.player.rotation;

        // Normalize rotation difference
        while (rotDiff > 3.14159f) rotDiff -= 2.0f * 3.14159f;
        while (rotDiff < -3.14159f) rotDiff += 2.0f * 3.14159f;

        app.player.rotation += rotDiff * Player::ROTATION_SPEED * deltaTime;

        // Move in movement direction
        app.player.position += moveDir * Player::MOVE_SPEED * deltaTime;
    }

    // Update terrain chunks based on player position
    app.terrain.update(app.player.position.x, app.player.position.z);

    // Get terrain height
    float terrainHeight = app.terrain.getHeightAt(app.player.position.x, app.player.position.z);

    // Check if player walked off an edge (terrain dropped below them)
    if (app.player.isGrounded && app.player.position.y > terrainHeight + 0.1f) {
        app.player.isGrounded = false;
        app.player.verticalVelocity = 0.0f;  // Start falling from current position
    }

    // Jump input
    if (Input::isKeyDown(Input::KEY_SPACE) && app.player.isGrounded) {
        app.player.verticalVelocity = Player::JUMP_FORCE;
        app.player.isGrounded = false;
    }

    // Apply gravity when not grounded
    if (!app.player.isGrounded) {
        app.player.verticalVelocity -= Player::GRAVITY * deltaTime;
        app.player.position.y += app.player.verticalVelocity * deltaTime;
    } else {
        // When grounded, follow terrain height smoothly
        app.player.position.y = terrainHeight;
    }

    // Ground collision
    if (app.player.position.y <= terrainHeight) {
        app.player.position.y = terrainHeight;
        app.player.verticalVelocity = 0.0f;
        app.player.isGrounded = true;
    }

    // Update character
    app.character.setPosition(app.player.position);
    app.character.setRotation(app.player.rotation);

    // Update camera to follow player
    app.camera.setTarget(app.player.position, app.player.rotation);
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
    waterModel.m[12] = floorf(cameraPos.x / 50.0f) * 50.0f;
    waterModel.m[14] = floorf(cameraPos.z / 50.0f) * 50.0f;
    Mat4 waterMvp = projection * view * waterModel;

    // Character model matrix
    Mat4 charModel;
    charModel.identity();
    // Translate to player position
    charModel.m[12] = app.player.position.x;
    charModel.m[13] = app.player.position.y;
    charModel.m[14] = app.player.position.z;
    // Rotate around Y axis
    float cosR = cosf(app.player.rotation);
    float sinR = sinf(app.player.rotation);
    charModel.m[0] = cosR;
    charModel.m[2] = -sinR;
    charModel.m[8] = sinR;
    charModel.m[10] = cosR;
    Mat4 charMvp = projection * view * charModel;

    // Light direction
    Vec3 lightDir = Vec3(0.5f, 0.8f, 0.3f).normalize();

    // Clear
    if (isUnderwater) {
        glClearColor(0.0f, 0.15f, 0.3f, 1.0f);
    } else {
        glClearColor(0.7f, 0.8f, 0.95f, 1.0f);
    }
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Draw skybox first (only if above water)
    if (!isUnderwater) {
        // Skybox needs view matrix without translation
        Mat4 skyView = view;
        skyView.m[12] = 0;  // Remove translation
        skyView.m[13] = 0;
        skyView.m[14] = 0;
        Mat4 skyMvp = projection * skyView;
        app.skybox.render(app.skyboxProgram, skyMvp, lightDir, time);
    }

    // Draw terrain
    app.terrain.render(app.terrainProgram, mvp, model, lightDir, cameraPos, time);

    // Get look direction for tree targeting
    Vec3 lookDir = app.camera.getLookDirection();

    // Find tree in crosshairs - start from player position, not camera
    // Use player eye level (position + some height offset)
    Vec3 playerEyePos = Vec3(
        app.player.position.x,
        app.player.position.y + 1.5f,  // Eye level
        app.player.position.z
    );
    bool hasTargetedTree = false;
    float rayLength = 5.0f;
    Vec3 targetedTreePos = app.terrain.findTreeInCrosshairs(playerEyePos, lookDir, rayLength, hasTargetedTree);

    // Draw debug ray
    {
        Vec3 rayEnd = Vec3(
            playerEyePos.x + lookDir.x * rayLength,
            playerEyePos.y + lookDir.y * rayLength,
            playerEyePos.z + lookDir.z * rayLength
        );
        float lineVertices[] = {
            playerEyePos.x, playerEyePos.y, playerEyePos.z,
            rayEnd.x, rayEnd.y, rayEnd.z
        };
        glBindBuffer(GL_ARRAY_BUFFER, app.debugLineVbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(lineVertices), lineVertices);

        glUseProgram(app.debugLineProgram);
        glUniformMatrix4fv(glGetUniformLocation(app.debugLineProgram, "uMVP"), 1, GL_FALSE, mvp.m);
        glUniform3f(glGetUniformLocation(app.debugLineProgram, "uColor"), 1.0f, 0.0f, 0.0f);  // Red ray

        glBindVertexArray(app.debugLineVao);
        glLineWidth(2.0f);
        glDrawArrays(GL_LINES, 0, 2);
    }

    // Draw trees
    Mat4 viewProjection = projection * view;
    app.terrain.renderTrees(app.treeProgram, viewProjection, lightDir, cameraPos, time, targetedTreePos, hasTargetedTree);

    // Draw houses (using tree shader - works for any vertex-colored mesh)
    app.terrain.renderHouses(app.treeProgram, viewProjection, lightDir, cameraPos, time);

    // Draw character (blobby jelly)
    glUseProgram(app.characterProgram);
    glUniformMatrix4fv(glGetUniformLocation(app.characterProgram, "uMVP"), 1, GL_FALSE, charMvp.m);
    glUniformMatrix4fv(glGetUniformLocation(app.characterProgram, "uModel"), 1, GL_FALSE, charModel.m);
    glUniform3f(glGetUniformLocation(app.characterProgram, "uLightDir"), lightDir.x, lightDir.y, lightDir.z);
    glUniform3f(glGetUniformLocation(app.characterProgram, "uCameraPos"), cameraPos.x, cameraPos.y, cameraPos.z);
    glUniform1f(glGetUniformLocation(app.characterProgram, "uTime"), time);
    // Pass movement speed for extra squish when moving
    Vec3 vel = app.player.position;  // We'll track this properly later
    float speed = Input::isKeyDown(Input::KEY_W) || Input::isKeyDown(Input::KEY_S) ||
                  Input::isKeyDown(Input::KEY_A) || Input::isKeyDown(Input::KEY_D) ? 1.0f : 0.0f;
    glUniform1f(glGetUniformLocation(app.characterProgram, "uSpeed"), speed);
    app.character.render();

    // Draw water surface
    if (isUnderwater) {
        glCullFace(GL_FRONT);
        glEnable(GL_CULL_FACE);
        glUseProgram(app.underwaterProgram);
        glUniformMatrix4fv(glGetUniformLocation(app.underwaterProgram, "uMVP"), 1, GL_FALSE, waterMvp.m);
        glUniform3f(glGetUniformLocation(app.underwaterProgram, "uCameraPos"), cameraPos.x, cameraPos.y, cameraPos.z);
        glUniform1f(glGetUniformLocation(app.underwaterProgram, "uTime"), time);
        app.water.renderDirect();
        glDisable(GL_CULL_FACE);
    } else {
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
