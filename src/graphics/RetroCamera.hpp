#pragma once

#include <raylib.h>

namespace retro {

class RetroCamera {
public:
  RetroCamera();

  void SetPosition(Vector3 position);
  void LookAt(Vector3 target);
  void SetFov(float degrees);
  // Lo mas cerca y lo mas lejos que se dibuja (m). Lo de raylib es 0.01 y 1000; una escena enorme
  // (la ciudad del cilindro, kilometros) pide mas lejos, y entonces un cerca menos pegado para que
  // la profundidad alcance.
  void SetClip(float nearPlane, float farPlane) noexcept {
    near_ = nearPlane;
    far_ = farPlane;
  }
  // Hacia donde queda "arriba" en la imagen (normalmente +Y). La pantalla de inicio que gira lo
  // cambia; SetView (la vista del jugador) lo vuelve a poner derecho.
  void SetUp(Vector3 up) { camera_.up = up; }
  Vector3 Up() const noexcept { return camera_.up; }
  float Fov() const noexcept { return camera_.fovy; }

  void UpdateOrbit(float dt);

  void Begin() const;
  void End() const;

  void SetView(Vector3 eye, float yawDegrees, float pitchDegrees);
  void OrbitAround(Vector3 target, float distance);

  const Camera3D& Raw() const noexcept { return camera_; }
  Vector3 Position() const noexcept { return camera_.position; }
  Vector3 Target() const noexcept { return camera_.target; }

private:
  void SyncOrbitFromCamera();
  void ApplyOrbit();

  Camera3D camera_{};
  float near_ = 0.01f;
  float far_ = 1000.0f;
  float distance_ = 7.0f;
  float yaw_ = 0.0f;
  float pitch_ = 0.5f;

  float mouseSensitivity_ = 0.005f;
  float keySpeed_ = 1.8f;
  float zoomStep_ = 0.6f;
};

} // namespace retro