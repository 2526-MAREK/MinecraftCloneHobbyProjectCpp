#pragma once

#include <DirectXMath.h>

#include "camera.h"
#include "input.h"
#include "world.h"

constexpr float kPlayerRadius = 0.3f;
constexpr float kPlayerHeight = 1.8f;
constexpr float kPlayerCrouchHeight = 1.1f;
constexpr float kPlayerEyeHeight = 1.6f;
constexpr float kPlayerCrouchEyeHeight = 0.9f;
constexpr float kPlayerGravity = -24.0f;
constexpr float kPlayerJumpSpeed = 8.0f;
constexpr float kPlayerStepHeight = 1.0f;

struct PlayerState {
  DirectX::XMFLOAT3 position;
  DirectX::XMFLOAT3 velocity;
  bool on_ground = false;
  bool crouching = false;
};

void InitPlayer(PlayerState& player, const DirectX::XMFLOAT3& position);
void UpdatePlayer(PlayerState& player, const World& world,
                  const CameraState& camera, const InputState& input, float dt);
DirectX::XMFLOAT3 GetPlayerEyePosition(const PlayerState& player);
bool WouldIntersectBlock(const PlayerState& player, int x, int y, int z);
