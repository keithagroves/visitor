#include "camera.h"
#include <cmath>

Camera::Camera() : position(0, 5, 0), yaw(0), pitch(0) {}

void Camera::setPosition(const Vec3& pos) {
    position = pos;
}

void Camera::rotate(float deltaYaw, float deltaPitch) {
    yaw += deltaYaw * MOUSE_SENSITIVITY;
    pitch -= deltaPitch * MOUSE_SENSITIVITY;

    if (pitch > PITCH_LIMIT) pitch = PITCH_LIMIT;
    if (pitch < -PITCH_LIMIT) pitch = -PITCH_LIMIT;
}

Vec3 Camera::getForward() const {
    return Vec3(sinf(yaw), 0.0f, -cosf(yaw)).normalize();
}

Vec3 Camera::getRight() const {
    return Vec3(cosf(yaw), 0.0f, sinf(yaw));
}

Vec3 Camera::getLookDirection() const {
    return Vec3(
        sinf(yaw) * cosf(pitch),
        sinf(pitch),
        -cosf(yaw) * cosf(pitch)
    );
}

void Camera::move(const Vec3& direction, float deltaTime) {
    position += direction * MOVE_SPEED * deltaTime;
}

void Camera::moveVertical(float amount, float deltaTime) {
    position.y += amount * MOVE_SPEED * deltaTime;
}

void Camera::clampToTerrain(float terrainHeight) {
    float minHeight = terrainHeight + DEFAULT_HEIGHT;
    if (position.y < minHeight) {
        position.y = minHeight;
    }
}

void Camera::clampToBounds(float bound) {
    if (position.x < -bound) position.x = -bound;
    if (position.x > bound) position.x = bound;
    if (position.z < -bound) position.z = -bound;
    if (position.z > bound) position.z = bound;
}

Mat4 Camera::getViewMatrix() const {
    Vec3 target = position + getLookDirection();
    Vec3 up(0, 1, 0);
    return Mat4::lookAt(position, target, up);
}
