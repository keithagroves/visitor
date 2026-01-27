#include "camera.h"
#include <cmath>

Camera::Camera() : position(0, 5, 0), target(0, 0, 0), yaw(0), pitch(0.3f), distance(CAMERA_DISTANCE) {}

void Camera::rotate(float deltaYaw, float deltaPitch) {
    yaw += deltaYaw * MOUSE_SENSITIVITY;
    pitch += deltaPitch * MOUSE_SENSITIVITY;  // Inverted for natural feel

    if (pitch > PITCH_LIMIT) pitch = PITCH_LIMIT;
    if (pitch < -0.2f) pitch = -0.2f;  // Don't go too far below horizontal
}

void Camera::zoom(float amount) {
    distance -= amount * 0.5f;
    if (distance < MIN_DISTANCE) distance = MIN_DISTANCE;
    if (distance > MAX_DISTANCE) distance = MAX_DISTANCE;
}

void Camera::setTarget(const Vec3& targetPos, float targetRotation) {
    target = targetPos;
    target.y += CAMERA_HEIGHT;  // Look at upper body, not feet

    // Calculate camera position behind and above target
    float camX = target.x - sinf(yaw) * cosf(pitch) * distance;
    float camY = target.y + sinf(pitch) * distance;
    float camZ = target.z + cosf(yaw) * cosf(pitch) * distance;

    position = Vec3(camX, camY, camZ);
}

Vec3 Camera::getForward() const {
    return Vec3(sinf(yaw), 0.0f, -cosf(yaw)).normalize();
}

Vec3 Camera::getRight() const {
    return Vec3(cosf(yaw), 0.0f, sinf(yaw));
}

Vec3 Camera::getLookDirection() const {
    return (target - position).normalize();
}

Mat4 Camera::getViewMatrix() const {
    Vec3 up(0, 1, 0);
    return Mat4::lookAt(position, target, up);
}
