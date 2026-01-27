#pragma once

#include "../math/vecmath.h"

class Camera {
public:
    static constexpr float DEFAULT_HEIGHT = 1.8f;  // Character eye height
    static constexpr float MOVE_SPEED = 6.0f;      // Walking speed
    static constexpr float MOUSE_SENSITIVITY = 0.002f;
    static constexpr float PITCH_LIMIT = 1.2f;     // ~70 degrees

    // Third-person camera settings
    static constexpr float CAMERA_DISTANCE = 5.0f;     // Distance behind character
    static constexpr float CAMERA_HEIGHT = 2.0f;       // Height above character
    static constexpr float MIN_DISTANCE = 2.0f;
    static constexpr float MAX_DISTANCE = 15.0f;

    Camera();

    void rotate(float deltaYaw, float deltaPitch);
    void zoom(float amount);

    // Third-person: set target to follow
    void setTarget(const Vec3& targetPos, float targetRotation);

    Vec3 getPosition() const { return position; }
    Vec3 getForward() const;
    Vec3 getRight() const;
    Vec3 getLookDirection() const;

    Mat4 getViewMatrix() const;

    float getYaw() const { return yaw; }
    float getPitch() const { return pitch; }
    float getDistance() const { return distance; }

private:
    Vec3 position;
    Vec3 target;
    float yaw = 0.0f;
    float pitch = 0.3f;  // Slight downward angle
    float distance = CAMERA_DISTANCE;
};
