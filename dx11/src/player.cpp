#include "player.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr float kCollisionEpsilon = 0.001f;

struct Aabb {
  float min_x;
  float min_y;
  float min_z;
  float max_x;
  float max_y;
  float max_z;
};

int MinCell(float value) {
  return static_cast<int>(std::floor(value + kCollisionEpsilon));
}

int MaxCell(float value) {
  return static_cast<int>(std::floor(value - kCollisionEpsilon));
}

float GetPlayerHeight(const PlayerState& player) {
  return player.crouching ? kPlayerCrouchHeight : kPlayerHeight;
}

Aabb MakeAabb(const PlayerState& player) {
  const float height = GetPlayerHeight(player);
  return {player.position.x - kPlayerRadius,
          player.position.y,
          player.position.z - kPlayerRadius,
          player.position.x + kPlayerRadius,
          player.position.y + height,
          player.position.z + kPlayerRadius};
}

bool IsSolid(const World& world, int x, int y, int z) {
  return GetBlock(world, x, y, z) != BlockId::Air;
}

bool IsAabbClear(const World& world, const Aabb& box) {
  const int min_x = MinCell(box.min_x);
  const int max_x = MaxCell(box.max_x);
  const int min_y = MinCell(box.min_y);
  const int max_y = MaxCell(box.max_y);
  const int min_z = MinCell(box.min_z);
  const int max_z = MaxCell(box.max_z);
  if (max_x < min_x || max_y < min_y || max_z < min_z) {
    return true;
  }
  for (int x = min_x; x <= max_x; ++x) {
    for (int y = min_y; y <= max_y; ++y) {
      for (int z = min_z; z <= max_z; ++z) {
        if (IsSolid(world, x, y, z)) {
          return false;
        }
      }
    }
  }
  return true;
}

bool CanStandUp(const PlayerState& player, const World& world) {
  PlayerState standing = player;
  standing.crouching = false;
  const Aabb box = MakeAabb(standing);
  return IsAabbClear(world, box);
}

bool MoveAlongX(PlayerState& player, const World& world, float delta) {
  if (delta == 0.0f) {
    return false;
  }
  Aabb box = MakeAabb(player);
  const int min_y = MinCell(box.min_y);
  const int max_y = MaxCell(box.max_y);
  const int min_z = MinCell(box.min_z);
  const int max_z = MaxCell(box.max_z);
  if (max_y < min_y || max_z < min_z) {
    player.position.x += delta;
    return false;
  }

  bool hit = false;
  float move = delta;
  if (delta > 0.0f) {
    const int start_x = static_cast<int>(std::floor(box.max_x + kCollisionEpsilon));
    const int end_x =
        static_cast<int>(std::floor(box.max_x + delta));
    for (int x = start_x; x <= end_x; ++x) {
      bool blocked = false;
      for (int y = min_y; y <= max_y && !blocked; ++y) {
        for (int z = min_z; z <= max_z; ++z) {
          if (IsSolid(world, x, y, z)) {
            blocked = true;
            break;
          }
        }
      }
      if (blocked) {
        float allowed = static_cast<float>(x) - box.max_x - kCollisionEpsilon;
        allowed = std::max(0.0f, allowed);
        if (allowed < move) {
          move = allowed;
        }
        hit = true;
        break;
      }
    }
  } else {
    const int start_x =
        static_cast<int>(std::floor(box.min_x - kCollisionEpsilon));
    const int end_x =
        static_cast<int>(std::floor(box.min_x + delta));
    for (int x = start_x; x >= end_x; --x) {
      bool blocked = false;
      for (int y = min_y; y <= max_y && !blocked; ++y) {
        for (int z = min_z; z <= max_z; ++z) {
          if (IsSolid(world, x, y, z)) {
            blocked = true;
            break;
          }
        }
      }
      if (blocked) {
        float allowed =
            static_cast<float>(x + 1) + kCollisionEpsilon - box.min_x;
        allowed = std::min(0.0f, allowed);
        if (allowed > move) {
          move = allowed;
        }
        hit = true;
        break;
      }
    }
  }
  player.position.x += move;
  return hit;
}

bool MoveAlongZ(PlayerState& player, const World& world, float delta) {
  if (delta == 0.0f) {
    return false;
  }
  Aabb box = MakeAabb(player);
  const int min_y = MinCell(box.min_y);
  const int max_y = MaxCell(box.max_y);
  const int min_x = MinCell(box.min_x);
  const int max_x = MaxCell(box.max_x);
  if (max_y < min_y || max_x < min_x) {
    player.position.z += delta;
    return false;
  }

  bool hit = false;
  float move = delta;
  if (delta > 0.0f) {
    const int start_z = static_cast<int>(std::floor(box.max_z + kCollisionEpsilon));
    const int end_z =
        static_cast<int>(std::floor(box.max_z + delta));
    for (int z = start_z; z <= end_z; ++z) {
      bool blocked = false;
      for (int y = min_y; y <= max_y && !blocked; ++y) {
        for (int x = min_x; x <= max_x; ++x) {
          if (IsSolid(world, x, y, z)) {
            blocked = true;
            break;
          }
        }
      }
      if (blocked) {
        float allowed = static_cast<float>(z) - box.max_z - kCollisionEpsilon;
        allowed = std::max(0.0f, allowed);
        if (allowed < move) {
          move = allowed;
        }
        hit = true;
        break;
      }
    }
  } else {
    const int start_z =
        static_cast<int>(std::floor(box.min_z - kCollisionEpsilon));
    const int end_z =
        static_cast<int>(std::floor(box.min_z + delta));
    for (int z = start_z; z >= end_z; --z) {
      bool blocked = false;
      for (int y = min_y; y <= max_y && !blocked; ++y) {
        for (int x = min_x; x <= max_x; ++x) {
          if (IsSolid(world, x, y, z)) {
            blocked = true;
            break;
          }
        }
      }
      if (blocked) {
        float allowed =
            static_cast<float>(z + 1) + kCollisionEpsilon - box.min_z;
        allowed = std::min(0.0f, allowed);
        if (allowed > move) {
          move = allowed;
        }
        hit = true;
        break;
      }
    }
  }
  player.position.z += move;
  return hit;
}

bool MoveAlongY(PlayerState& player, const World& world, float delta) {
  if (delta == 0.0f) {
    return false;
  }
  Aabb box = MakeAabb(player);
  const int min_x = MinCell(box.min_x);
  const int max_x = MaxCell(box.max_x);
  const int min_z = MinCell(box.min_z);
  const int max_z = MaxCell(box.max_z);
  if (max_x < min_x || max_z < min_z) {
    player.position.y += delta;
    return false;
  }

  bool hit = false;
  float move = delta;
  if (delta > 0.0f) {
    const int start_y = static_cast<int>(std::floor(box.max_y + kCollisionEpsilon));
    const int end_y =
        static_cast<int>(std::floor(box.max_y + delta));
    for (int y = start_y; y <= end_y; ++y) {
      bool blocked = false;
      for (int x = min_x; x <= max_x && !blocked; ++x) {
        for (int z = min_z; z <= max_z; ++z) {
          if (IsSolid(world, x, y, z)) {
            blocked = true;
            break;
          }
        }
      }
      if (blocked) {
        float allowed = static_cast<float>(y) - box.max_y - kCollisionEpsilon;
        allowed = std::max(0.0f, allowed);
        if (allowed < move) {
          move = allowed;
        }
        hit = true;
        break;
      }
    }
  } else {
    const int start_y =
        static_cast<int>(std::floor(box.min_y - kCollisionEpsilon));
    const int end_y =
        static_cast<int>(std::floor(box.min_y + delta));
    for (int y = start_y; y >= end_y; --y) {
      bool blocked = false;
      for (int x = min_x; x <= max_x && !blocked; ++x) {
        for (int z = min_z; z <= max_z; ++z) {
          if (IsSolid(world, x, y, z)) {
            blocked = true;
            break;
          }
        }
      }
      if (blocked) {
        float allowed =
            static_cast<float>(y + 1) + kCollisionEpsilon - box.min_y;
        allowed = std::min(0.0f, allowed);
        if (allowed > move) {
          move = allowed;
        }
        hit = true;
        break;
      }
    }
  }
  player.position.y += move;
  return hit;
}
}  // namespace

void InitPlayer(PlayerState& player, const DirectX::XMFLOAT3& position) {
  player.position = position;
  player.velocity = {0.0f, 0.0f, 0.0f};
  player.on_ground = false;
  player.crouching = false;
}

void UpdatePlayer(PlayerState& player, const World& world,
                  const CameraState& camera, const InputState& input, float dt) {
  const bool input_active = input.mouse_captured;
  const int move_forward = input_active ? input.move_forward : 0;
  const int move_right = input_active ? input.move_right : 0;
  const bool jump_pressed = input_active && input.jump_pressed;
  const bool crouch_down = input_active && input.crouch_down;

  if (crouch_down) {
    player.crouching = true;
  } else if (player.crouching && CanStandUp(player, world)) {
    player.crouching = false;
  }

  DirectX::XMVECTOR forward = GetCameraForward(camera);
  forward = DirectX::XMVectorSetY(forward, 0.0f);
  const float forward_len =
      DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(forward));
  if (forward_len > 0.0001f) {
    forward = DirectX::XMVector3Normalize(forward);
  } else {
    forward = DirectX::XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
  }
  const DirectX::XMVECTOR up = DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
  const DirectX::XMVECTOR right =
      DirectX::XMVector3Normalize(DirectX::XMVector3Cross(up, forward));

  DirectX::XMVECTOR wish = DirectX::XMVectorZero();
  if (move_forward != 0) {
    wish = DirectX::XMVectorAdd(
        wish,
        DirectX::XMVectorScale(forward, static_cast<float>(move_forward)));
  }
  if (move_right != 0) {
    wish = DirectX::XMVectorAdd(
        wish, DirectX::XMVectorScale(right, static_cast<float>(move_right)));
  }
  if (DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(wish)) > 0.0001f) {
    wish = DirectX::XMVector3Normalize(wish);
  } else {
    wish = DirectX::XMVectorZero();
  }

  DirectX::XMFLOAT3 wish_dir{};
  DirectX::XMStoreFloat3(&wish_dir, wish);
  float speed = camera.move_speed * (input.speed_boost ? 1.7f : 1.0f);
  if (player.crouching) {
    speed *= 0.45f;
  }
  player.velocity.x = wish_dir.x * speed;
  player.velocity.z = wish_dir.z * speed;

  if (player.on_ground && jump_pressed) {
    player.velocity.y = kPlayerJumpSpeed;
    player.on_ground = false;
  }

  player.velocity.y += kPlayerGravity * dt;

  const float dx = player.velocity.x * dt;
  const float dz = player.velocity.z * dt;
  const float dy = player.velocity.y * dt;

  PlayerState pre_step = player;
  bool hit_x = MoveAlongX(player, world, dx);
  bool hit_z = MoveAlongZ(player, world, dz);

  bool stepped = false;
  if (player.on_ground && (hit_x || hit_z)) {
    PlayerState step = pre_step;
    if (!MoveAlongY(step, world, kPlayerStepHeight)) {
      const bool step_hit_x = MoveAlongX(step, world, dx);
      const bool step_hit_z = MoveAlongZ(step, world, dz);
      if (!step_hit_x && !step_hit_z) {
        MoveAlongY(step, world, -(kPlayerStepHeight + kCollisionEpsilon));
        player = step;
        stepped = true;
      }
    }
  }

  if (!stepped) {
    if (hit_x) {
      player.velocity.x = 0.0f;
    }
    if (hit_z) {
      player.velocity.z = 0.0f;
    }
  }

  bool hit_y = MoveAlongY(player, world, dy);
  if (hit_y) {
    if (dy < 0.0f) {
      player.on_ground = true;
    }
    player.velocity.y = 0.0f;
  } else {
    player.on_ground = false;
  }
}

DirectX::XMFLOAT3 GetPlayerEyePosition(const PlayerState& player) {
  const float eye =
      player.crouching ? kPlayerCrouchEyeHeight : kPlayerEyeHeight;
  return {player.position.x, player.position.y + eye, player.position.z};
}

bool WouldIntersectBlock(const PlayerState& player, int x, int y, int z) {
  const Aabb box = MakeAabb(player);
  const float min_x = static_cast<float>(x);
  const float min_y = static_cast<float>(y);
  const float min_z = static_cast<float>(z);
  const float max_x = min_x + kBlockSize;
  const float max_y = min_y + kBlockSize;
  const float max_z = min_z + kBlockSize;
  const bool overlap =
      (box.min_x < max_x && box.max_x > min_x && box.min_y < max_y &&
       box.max_y > min_y && box.min_z < max_z && box.max_z > min_z);
  return overlap;
}
