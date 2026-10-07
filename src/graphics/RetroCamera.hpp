#pragma once

#include <raylib.h>

namespace retro {

class RetroCamera {
public:
  RetroCamera();

  void SetPosition(Vector3 position);
  void LookAt(Vector3 target);
  void SetFov(float degrees);

  void UpdateOrbit(float dt);

  void Begin() const;
  void End() const;

  const Camera3D& Raw() const noexcept { return camera_; }
  Vector3 Position() const noexcept { return camera_.position; }
  Vector3 Target() const noexcept { return camera_.target; }

private:
  void SyncOrbitFromCamera();
  void ApplyOrbit();

  Camera3D camera_{};
  float distance_ = 7.0f;
  float yaw_ = 0.0f;
  float pitch_ = 0.5f;

  float mouseSensitivity_ = 0.005f;
  float keySpeed_ = 1.8f;
  float zoomStep_ = 0.6f;
};

} // namespace retro