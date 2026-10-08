#include "graphics/RetroCamera.hpp"

#include <raymath.h>

#include <algorithm>
#include <cmath>

namespace retro {

namespace {
constexpr float kMaxPitch = 1.50f;
constexpr float kMinDistance = 1.5f;
constexpr float kMaxDistance = 40.0f;
} // namespace

RetroCamera::RetroCamera() {
  camera_.position = {5.0f, 4.0f, 5.0f};
  camera_.target = {0.0f, 0.5f, 0.0f};
  camera_.up = {0.0f, 1.0f, 0.0f};
  camera_.fovy = 60.0f;
  camera_.projection = CAMERA_PERSPECTIVE;
  SyncOrbitFromCamera();
}

void RetroCamera::SetPosition(Vector3 position) {
  camera_.position = position;
  SyncOrbitFromCamera();
}

void RetroCamera::LookAt(Vector3 target) {
  camera_.target = target;
  SyncOrbitFromCamera();
}

void RetroCamera::SetFov(float degrees) { camera_.fovy = degrees; }

void RetroCamera::SetView(Vector3 eye, float yawDegrees, float pitchDegrees) {
  const float yaw = yawDegrees * DEG2RAD;
  const float pitch = pitchDegrees * DEG2RAD;

  const Vector3 forward = {-std::sin(yaw) * std::cos(pitch), std::sin(pitch),
                           -std::cos(yaw) * std::cos(pitch)};

  camera_.position = eye;
  camera_.target = Vector3Add(eye, forward);
  SyncOrbitFromCamera();
}

void RetroCamera::OrbitAround(Vector3 target, float distance) {
  camera_.target = target;
  distance_ = std::clamp(distance, kMinDistance, kMaxDistance);
  ApplyOrbit();
}

void RetroCamera::Begin() const { BeginMode3D(camera_); }
void RetroCamera::End() const { EndMode3D(); }

void RetroCamera::SyncOrbitFromCamera() {
  const Vector3 offset = Vector3Subtract(camera_.position, camera_.target);
  const float length = Vector3Length(offset);
  if (length < 1e-4f)
    return;

  distance_ = length;
  yaw_ = std::atan2(offset.x, offset.y);
  pitch_ = std::asin(offset.y / length);
}

void RetroCamera::ApplyOrbit() {
  const float cp = std::cos(pitch_);
  camera_.position = {camera_.target.x + distance_ * cp * std::sin(yaw_),
                      camera_.target.y + distance_ * std::sin(pitch_),
                      camera_.target.z + distance_ * cp * std::cos(yaw_)};
}

void RetroCamera::UpdateOrbit(float dt) {
  if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
    const Vector2 delta = GetMouseDelta();
    yaw_ -= delta.x * mouseSensitivity_;
    pitch_ += delta.y * mouseSensitivity_;
  }

  if (IsKeyDown(KEY_LEFT))
    yaw_ += keySpeed_ * dt;
  if (IsKeyDown(KEY_RIGHT))
    yaw_ -= keySpeed_ * dt;
  if (IsKeyDown(KEY_UP))
    pitch_ += keySpeed_ * dt;
  if (IsKeyDown(KEY_DOWN))
    pitch_ -= keySpeed_ * dt;

  distance_ -= GetMouseWheelMove() * zoomStep_;

  pitch_ = std::clamp(pitch_, -kMaxPitch, kMaxPitch);
  distance_ = std::clamp(distance_, kMinDistance, kMaxDistance);

  ApplyOrbit();
}

} // namespace retro
