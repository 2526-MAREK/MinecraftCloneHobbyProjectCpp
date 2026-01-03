#pragma once

#include <DirectXMath.h>

#include "input.h"

constexpr float kMoveSpeed = 6.0f;
constexpr float kMouseSensitivity = 0.002f;
constexpr float kMaxPitch = DirectX::XM_PIDIV2 - 0.01f;

struct CameraState {
  DirectX::XMFLOAT3 position;
  float yaw;
  float pitch;
  float move_speed;
  float mouse_sensitivity;
};

DirectX::XMVECTOR GetCameraForward(const CameraState& camera);
void UpdateCamera(CameraState& camera, const InputState& input, float dt);
