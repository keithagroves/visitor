#pragma once

#include "../math/vecmath.h"

class Camera {
public:
    static constexpr float DEFAULT_HEIGHT = 1.8f;  // Human eye height (~6 feet)
    static constexpr float MOVE_SPEED = 6.0f;      // Walking speed
    static constexpr float MOUSE_SENSITIVITY = 0.002f;
    static constexpr float PITCH_LIMIT = 1.4f;  // ~80 degrees

    Camera();

    void setPosition(const Vec3& pos);
    void rotate(float deltaYaw, float deltaPitch);
    void move(const Vec3& direction, float deltaTime);
    void moveVertical(float amount, float deltaTime);

    void clampToTerrain(float terrainHeight);
    void clampToBounds(float bound);

    Vec3 getPosition() const { return position; }
    Vec3 getForward() const;
    Vec3 getRight() const;
    Vec3 getLookDirection() const;

    Mat4 getViewMatrix() const;

    float getYaw() const { return yaw; }
    float getPitch() const { return pitch; }

private:
    Vec3 position;
    float yaw = 0.0f;
    float pitch = 0.0f;
};
