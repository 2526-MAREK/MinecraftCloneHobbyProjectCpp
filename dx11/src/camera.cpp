#include "camera.h"

#include <algorithm>
#include <cmath>

DirectX::XMVECTOR GetCameraForward(const CameraState& camera) {
  const float cos_pitch = std::cos(camera.pitch);
  const float sin_pitch = std::sin(camera.pitch);
  const float sin_yaw = std::sin(camera.yaw);
  const float cos_yaw = std::cos(camera.yaw);
  return DirectX::XMVector3Normalize(DirectX::XMVectorSet(
      cos_pitch * sin_yaw, sin_pitch, cos_pitch * cos_yaw, 0.0f));
}

void UpdateCameraLook(CameraState& camera, const InputState& input) {
  if (!input.mouse_captured) {
    return;
  }
  if (input.mouse_dx == 0.0f && input.mouse_dy == 0.0f) {
    return;
  }
  camera.yaw += input.mouse_dx * camera.mouse_sensitivity;
  camera.pitch -= input.mouse_dy * camera.mouse_sensitivity;
  camera.pitch = std::clamp(camera.pitch, -kMaxPitch, kMaxPitch);
}

void UpdateCamera(CameraState& camera, const InputState& input, float dt) {
  if (!input.mouse_captured) {
    return;
  }

  UpdateCameraLook(camera, input);

  const DirectX::XMVECTOR forward = GetCameraForward(camera);
  const DirectX::XMVECTOR up = DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
  const DirectX::XMVECTOR right =
      DirectX::XMVector3Normalize(DirectX::XMVector3Cross(up, forward));

  DirectX::XMVECTOR move = DirectX::XMVectorZero();
  if (input.move_forward != 0) {
    move = DirectX::XMVectorAdd(
        move, DirectX::XMVectorScale(forward,
                                     static_cast<float>(input.move_forward)));
  }
  if (input.move_right != 0) {
    move = DirectX::XMVectorAdd(
        move,
        DirectX::XMVectorScale(right, static_cast<float>(input.move_right)));
  }
  if (input.move_up != 0) {
    move = DirectX::XMVectorAdd(
        move, DirectX::XMVectorScale(up, static_cast<float>(input.move_up)));
  }

  const float speed =
      input.speed_boost ? (camera.move_speed * 3.0f) : camera.move_speed;
  const float move_len_sq =
      DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(move));
  if (move_len_sq > 0.0001f) {
    move = DirectX::XMVector3Normalize(move);
    DirectX::XMVECTOR pos = DirectX::XMLoadFloat3(&camera.position);
    pos = DirectX::XMVectorAdd(pos, DirectX::XMVectorScale(move, speed * dt));
    DirectX::XMStoreFloat3(&camera.position, pos);
  }
}
