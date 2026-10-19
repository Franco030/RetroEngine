#include "EditorCamera.hpp"

#include <raymath.h>

#include <algorithm>
#include <cmath>

namespace editor {

EditorCamera::EditorCamera() { LookFrom({7.0f, 5.0f, 9.0f}, {0.0f, 1.0f, 0.0f}); }

EditorCamera::~EditorCamera() {
  if (looking_)
    EnableCursor();
}

Vector3 EditorCamera::Forward() const {
  const float yaw = yaw_ * DEG2RAD;
  const float pitch = pitch_ * DEG2RAD;
  return {-std::sin(yaw) * std::cos(pitch), std::sin(pitch), -std::cos(yaw) * std::cos(pitch)};
}

void EditorCamera::LookFrom(Vector3 position, Vector3 target) {
  pos_ = position;
  const Vector3 d = Vector3Normalize(Vector3Subtract(target, position));
  yaw_ = std::atan2(-d.x, -d.z) * RAD2DEG;
  pitch_ = std::asin(std::clamp(d.y, -1.0f, 1.0f)) * RAD2DEG;
}

void EditorCamera::FocusOn(Vector3 target, float distance) {
  pos_ = Vector3Subtract(target, Vector3Scale(Forward(), distance));
}

Camera3D EditorCamera::Raw() const {
  Camera3D cam{};
  cam.position = pos_;
  cam.target = Vector3Add(pos_, Forward());
  cam.up = {0.0f, 1.0f, 0.0f};
  cam.fovy = fov;
  cam.projection = CAMERA_PERSPECTIVE;
  return cam;
}

bool EditorCamera::Update(float dt, bool hovered) {
  if (!looking_ && hovered && IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
    looking_ = true;
    skipFrames_ = 2;
    DisableCursor();
  }
  if (looking_ && !IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
    looking_ = false;
    EnableCursor();
  }

  const Vector3 fwd = Forward();
  const Vector3 right = Vector3Normalize(Vector3CrossProduct(fwd, {0.0f, 1.0f, 0.0f}));
  const Vector3 up = Vector3CrossProduct(right, fwd);

  if (looking_) {
    const Vector2 d = GetMouseDelta();
    if (skipFrames_ > 0) {
      --skipFrames_;
    } else {
      yaw_ -= d.x * 0.15f;
      pitch_ = std::clamp(pitch_ - d.y * 0.15f, -89.0f, 89.0f);
    }

    Vector3 move{0.0f, 0.0f, 0.0f};
    if (IsKeyDown(KEY_W))
      move = Vector3Add(move, fwd);
    if (IsKeyDown(KEY_S))
      move = Vector3Subtract(move, fwd);
    if (IsKeyDown(KEY_D))
      move = Vector3Add(move, right);
    if (IsKeyDown(KEY_A))
      move = Vector3Subtract(move, right);
    if (IsKeyDown(KEY_E))
      move.y += 1.0f;
    if (IsKeyDown(KEY_Q))
      move.y -= 1.0f;

    const float speed = speed_ * (IsKeyDown(KEY_LEFT_SHIFT) ? 3.0f : 1.0f);
    if (Vector3Length(move) > 0.0f) {
      pos_ = Vector3Add(pos_, Vector3Scale(Vector3Normalize(move), speed * dt));
    }

    const float wheel = GetMouseWheelMove();
    if (wheel != 0.0f)
      speed_ = std::clamp(speed_ * std::pow(1.2f, wheel), 0.5f, 100.0f);
  } else if (hovered) {
    const float wheel = GetMouseWheelMove();
    if (wheel != 0.0f)
      pos_ = Vector3Add(pos_, Vector3Scale(fwd, wheel * 1.5f));

    if (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) {
      const Vector2 d = GetMouseDelta();
      pos_ = Vector3Subtract(pos_, Vector3Scale(right, d.x * 0.02f));
      pos_ = Vector3Add(pos_, Vector3Scale(up, d.y * 0.02f));
    }
  }
  return looking_;
}

} // namespace editor
