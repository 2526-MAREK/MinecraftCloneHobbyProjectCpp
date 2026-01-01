#include "raylib.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <unordered_map>
#include <vector>

namespace {
constexpr int kScreenWidth = 1280;
constexpr int kScreenHeight = 720;
constexpr int kChunkSize = 16;
constexpr int kChunkVolume = kChunkSize * kChunkSize * kChunkSize;
constexpr int kWorldRadiusChunks = 2;
constexpr int kGroundHeight = 2;
constexpr float kBlockSize = 1.0f;
constexpr float kRaycastDistance = 8.0f;

enum class BlockId : uint8_t {
  Air = 0,
  Grass = 1,
  Dirt = 2,
  Stone = 3,
};

struct Int3 {
  int x;
  int y;
  int z;
};

bool operator==(const Int3& a, const Int3& b) {
  return a.x == b.x && a.y == b.y && a.z == b.z;
}

struct Int3Hash {
  size_t operator()(const Int3& value) const noexcept {
    size_t seed = std::hash<int>{}(value.x);
    seed ^= std::hash<int>{}(value.y) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    seed ^= std::hash<int>{}(value.z) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    return seed;
  }
};

int FloorDiv(int value, int divisor) {
  const int quot = value / divisor;
  const int rem = value % divisor;
  if (rem == 0) {
    return quot;
  }
  const bool needs_adjust = (rem < 0) != (divisor < 0);
  return needs_adjust ? quot - 1 : quot;
}

int Mod(int value, int divisor) {
  int mod = value % divisor;
  if (mod < 0) {
    mod += divisor;
  }
  return mod;
}

int ToIndex(int x, int y, int z) {
  return x + (y * kChunkSize) + (z * kChunkSize * kChunkSize);
}

Color ApplyShade(Color color, float shade) {
  const auto clamp = [](float value) {
    return static_cast<unsigned char>(std::clamp(value, 0.0f, 255.0f));
  };
  return {
      clamp(color.r * shade),
      clamp(color.g * shade),
      clamp(color.b * shade),
      color.a,
  };
}

enum class FaceDir {
  PosX,
  NegX,
  PosY,
  NegY,
  PosZ,
  NegZ,
};

struct FaceDef {
  Int3 neighbor;
  Vector3 normal;
  std::array<Vector3, 4> corners;
  FaceDir dir;
};

const std::array<FaceDef, 6> kFaces = {{
    {{1, 0, 0}, {1.0f, 0.0f, 0.0f}, {{{1.0f, 0.0f, 0.0f},
                                      {1.0f, 1.0f, 0.0f},
                                      {1.0f, 1.0f, 1.0f},
                                      {1.0f, 0.0f, 1.0f}}},
     FaceDir::PosX},
    {{-1, 0, 0}, {-1.0f, 0.0f, 0.0f}, {{{0.0f, 0.0f, 1.0f},
                                       {0.0f, 1.0f, 1.0f},
                                       {0.0f, 1.0f, 0.0f},
                                       {0.0f, 0.0f, 0.0f}}},
     FaceDir::NegX},
    {{0, 1, 0}, {0.0f, 1.0f, 0.0f}, {{{0.0f, 1.0f, 0.0f},
                                     {0.0f, 1.0f, 1.0f},
                                     {1.0f, 1.0f, 1.0f},
                                     {1.0f, 1.0f, 0.0f}}},
     FaceDir::PosY},
    {{0, -1, 0}, {0.0f, -1.0f, 0.0f}, {{{0.0f, 0.0f, 1.0f},
                                       {0.0f, 0.0f, 0.0f},
                                       {1.0f, 0.0f, 0.0f},
                                       {1.0f, 0.0f, 1.0f}}},
     FaceDir::NegY},
    {{0, 0, 1}, {0.0f, 0.0f, 1.0f}, {{{0.0f, 0.0f, 1.0f},
                                     {1.0f, 0.0f, 1.0f},
                                     {1.0f, 1.0f, 1.0f},
                                     {0.0f, 1.0f, 1.0f}}},
     FaceDir::PosZ},
    {{0, 0, -1}, {0.0f, 0.0f, -1.0f}, {{{1.0f, 0.0f, 0.0f},
                                       {0.0f, 0.0f, 0.0f},
                                       {0.0f, 1.0f, 0.0f},
                                       {1.0f, 1.0f, 0.0f}}},
     FaceDir::NegZ},
}};

Color BlockFaceColor(BlockId id, FaceDir face) {
  switch (id) {
    case BlockId::Grass:
      if (face == FaceDir::PosY) {
        return {90, 170, 90, 255};
      }
      if (face == FaceDir::NegY) {
        return {110, 85, 60, 255};
      }
      return {80, 150, 80, 255};
    case BlockId::Dirt:
      return {120, 90, 60, 255};
    case BlockId::Stone:
      return {130, 130, 130, 255};
    case BlockId::Air:
      break;
  }
  return {0, 0, 0, 0};
}

struct Chunk {
  Int3 coord;
  std::vector<BlockId> blocks;
  Model model{};
  bool mesh_dirty = true;
  bool mesh_ready = false;

  explicit Chunk(Int3 coord_in)
      : coord(coord_in), blocks(kChunkVolume, BlockId::Air) {}

  BlockId Get(int x, int y, int z) const { return blocks[ToIndex(x, y, z)]; }
  void Set(int x, int y, int z, BlockId id) { blocks[ToIndex(x, y, z)] = id; }
};

class World {
 public:
  void GenerateFlat(int radius) {
    Clear();
    for (int cz = -radius; cz <= radius; ++cz) {
      for (int cx = -radius; cx <= radius; ++cx) {
        Chunk& chunk = GetOrCreateChunk({cx, 0, cz});
        for (int z = 0; z < kChunkSize; ++z) {
          for (int x = 0; x < kChunkSize; ++x) {
            for (int y = 0; y < kGroundHeight; ++y) {
              BlockId id = (y == kGroundHeight - 1) ? BlockId::Grass : BlockId::Dirt;
              chunk.Set(x, y, z, id);
            }
          }
        }
        chunk.mesh_dirty = true;
      }
    }
    RebuildDirtyMeshes();
  }

  void Clear() {
    for (auto& [coord, chunk] : chunks_) {
      if (chunk.mesh_ready) {
        UnloadModel(chunk.model);
        chunk.mesh_ready = false;
      }
    }
    chunks_.clear();
  }

  BlockId GetBlock(int wx, int wy, int wz) const {
    const Int3 chunk_coord{FloorDiv(wx, kChunkSize), FloorDiv(wy, kChunkSize),
                           FloorDiv(wz, kChunkSize)};
    const Int3 local{Mod(wx, kChunkSize), Mod(wy, kChunkSize), Mod(wz, kChunkSize)};
    const Chunk* chunk = GetChunk(chunk_coord);
    if (!chunk) {
      return BlockId::Air;
    }
    return chunk->Get(local.x, local.y, local.z);
  }

  bool SetBlock(int wx, int wy, int wz, BlockId id) {
    const Int3 chunk_coord{FloorDiv(wx, kChunkSize), FloorDiv(wy, kChunkSize),
                           FloorDiv(wz, kChunkSize)};
    const Int3 local{Mod(wx, kChunkSize), Mod(wy, kChunkSize), Mod(wz, kChunkSize)};
    Chunk* chunk = nullptr;
    if (id == BlockId::Air) {
      chunk = GetChunk(chunk_coord);
      if (!chunk) {
        return false;
      }
    } else {
      chunk = &GetOrCreateChunk(chunk_coord);
    }
    if (chunk->Get(local.x, local.y, local.z) == id) {
      return false;
    }
    chunk->Set(local.x, local.y, local.z, id);
    MarkDirtyNeighbors(chunk_coord, local.x, local.y, local.z);
    return true;
  }

  void RebuildDirtyMeshes() {
    for (auto& [coord, chunk] : chunks_) {
      if (chunk.mesh_dirty) {
        BuildChunkMesh(chunk);
      }
    }
  }

  void Draw() const {
    for (const auto& [coord, chunk] : chunks_) {
      if (!chunk.mesh_ready) {
        continue;
      }
      const Vector3 origin{
          coord.x * kChunkSize * kBlockSize,
          coord.y * kChunkSize * kBlockSize,
          coord.z * kChunkSize * kBlockSize,
      };
      DrawModel(chunk.model, origin, 1.0f, WHITE);
    }
  }

 private:
  struct MeshBuffers {
    std::vector<float> positions;
    std::vector<float> normals;
    std::vector<float> texcoords;
    std::vector<unsigned char> colors;
  };

  Chunk* GetChunk(Int3 coord) {
    auto it = chunks_.find(coord);
    return it != chunks_.end() ? &it->second : nullptr;
  }

  const Chunk* GetChunk(Int3 coord) const {
    auto it = chunks_.find(coord);
    return it != chunks_.end() ? &it->second : nullptr;
  }

  Chunk& GetOrCreateChunk(Int3 coord) {
    auto it = chunks_.find(coord);
    if (it != chunks_.end()) {
      return it->second;
    }
    return chunks_.emplace(coord, Chunk{coord}).first->second;
  }

  void MarkChunkDirty(Int3 coord) {
    auto it = chunks_.find(coord);
    if (it != chunks_.end()) {
      it->second.mesh_dirty = true;
    }
  }

  void MarkDirtyNeighbors(Int3 coord, int lx, int ly, int lz) {
    MarkChunkDirty(coord);
    if (lx == 0) {
      MarkChunkDirty({coord.x - 1, coord.y, coord.z});
    } else if (lx == kChunkSize - 1) {
      MarkChunkDirty({coord.x + 1, coord.y, coord.z});
    }
    if (ly == 0) {
      MarkChunkDirty({coord.x, coord.y - 1, coord.z});
    } else if (ly == kChunkSize - 1) {
      MarkChunkDirty({coord.x, coord.y + 1, coord.z});
    }
    if (lz == 0) {
      MarkChunkDirty({coord.x, coord.y, coord.z - 1});
    } else if (lz == kChunkSize - 1) {
      MarkChunkDirty({coord.x, coord.y, coord.z + 1});
    }
  }

  void AddVertex(MeshBuffers& buffers, Vector3 position, Vector3 normal, Vector2 uv,
                 Color color) {
    buffers.positions.insert(buffers.positions.end(), {position.x, position.y, position.z});
    buffers.normals.insert(buffers.normals.end(), {normal.x, normal.y, normal.z});
    buffers.texcoords.insert(buffers.texcoords.end(), {uv.x, uv.y});
    buffers.colors.insert(buffers.colors.end(), {color.r, color.g, color.b, color.a});
  }

  void AddFace(MeshBuffers& buffers, const Vector3& base, const FaceDef& face,
               Color color) {
    static constexpr std::array<Vector2, 4> uvs = {{
        {0.0f, 0.0f},
        {1.0f, 0.0f},
        {1.0f, 1.0f},
        {0.0f, 1.0f},
    }};
    constexpr int indices[6] = {0, 1, 2, 0, 2, 3};
    for (int i = 0; i < 6; ++i) {
      const int idx = indices[i];
      const Vector3 corner = face.corners[idx];
      const Vector3 position{
          base.x + corner.x * kBlockSize,
          base.y + corner.y * kBlockSize,
          base.z + corner.z * kBlockSize,
      };
      AddVertex(buffers, position, face.normal, uvs[idx], color);
    }
  }

  void BuildChunkMesh(Chunk& chunk) {
    if (chunk.mesh_ready) {
      UnloadModel(chunk.model);
      chunk.mesh_ready = false;
    }

    MeshBuffers buffers;
    const Int3 chunk_origin{chunk.coord.x * kChunkSize, chunk.coord.y * kChunkSize,
                            chunk.coord.z * kChunkSize};

    for (int z = 0; z < kChunkSize; ++z) {
      for (int y = 0; y < kChunkSize; ++y) {
        for (int x = 0; x < kChunkSize; ++x) {
          const BlockId id = chunk.Get(x, y, z);
          if (id == BlockId::Air) {
            continue;
          }
          const int wx = chunk_origin.x + x;
          const int wy = chunk_origin.y + y;
          const int wz = chunk_origin.z + z;
          const Vector3 base{static_cast<float>(x) * kBlockSize,
                             static_cast<float>(y) * kBlockSize,
                             static_cast<float>(z) * kBlockSize};
          for (const auto& face : kFaces) {
            const BlockId neighbor =
                GetBlock(wx + face.neighbor.x, wy + face.neighbor.y, wz + face.neighbor.z);
            if (neighbor == BlockId::Air) {
              const Color base_color = BlockFaceColor(id, face.dir);
              const float shade =
                  (face.dir == FaceDir::PosY) ? 1.0f : (face.dir == FaceDir::NegY ? 0.6f : 0.85f);
              AddFace(buffers, base, face, ApplyShade(base_color, shade));
            }
          }
        }
      }
    }

    if (buffers.positions.empty()) {
      chunk.mesh_dirty = false;
      return;
    }

    Mesh mesh{};
    mesh.vertexCount = static_cast<int>(buffers.positions.size() / 3);
    mesh.triangleCount = mesh.vertexCount / 3;
    mesh.vertices = static_cast<float*>(MemAlloc(
        static_cast<unsigned int>(buffers.positions.size() * sizeof(float))));
    mesh.normals = static_cast<float*>(
        MemAlloc(static_cast<unsigned int>(buffers.normals.size() * sizeof(float))));
    mesh.texcoords = static_cast<float*>(
        MemAlloc(static_cast<unsigned int>(buffers.texcoords.size() * sizeof(float))));
    mesh.colors = static_cast<unsigned char*>(
        MemAlloc(static_cast<unsigned int>(buffers.colors.size() * sizeof(unsigned char))));
    std::memcpy(mesh.vertices, buffers.positions.data(),
                buffers.positions.size() * sizeof(float));
    std::memcpy(mesh.normals, buffers.normals.data(),
                buffers.normals.size() * sizeof(float));
    std::memcpy(mesh.texcoords, buffers.texcoords.data(),
                buffers.texcoords.size() * sizeof(float));
    std::memcpy(mesh.colors, buffers.colors.data(),
                buffers.colors.size() * sizeof(unsigned char));

    UploadMesh(&mesh, false);
    chunk.model = LoadModelFromMesh(mesh);
    chunk.mesh_ready = true;
    chunk.mesh_dirty = false;
  }

  std::unordered_map<Int3, Chunk, Int3Hash> chunks_;
};

struct RayHit {
  bool hit = false;
  Int3 block{0, 0, 0};
  Int3 previous{0, 0, 0};
  Vector3 normal{0.0f, 0.0f, 0.0f};
};

int FloorToInt(float value) {
  return static_cast<int>(std::floor(value));
}

RayHit RaycastVoxels(const World& world, Vector3 origin, Vector3 direction,
                     float max_distance) {
  RayHit result;
  const float length =
      std::sqrt(direction.x * direction.x + direction.y * direction.y +
                direction.z * direction.z);
  if (length <= 0.0f) {
    return result;
  }
  direction.x /= length;
  direction.y /= length;
  direction.z /= length;

  Int3 current{FloorToInt(origin.x), FloorToInt(origin.y), FloorToInt(origin.z)};
  Int3 previous = current;

  const int step_x = (direction.x > 0.0f) ? 1 : (direction.x < 0.0f ? -1 : 0);
  const int step_y = (direction.y > 0.0f) ? 1 : (direction.y < 0.0f ? -1 : 0);
  const int step_z = (direction.z > 0.0f) ? 1 : (direction.z < 0.0f ? -1 : 0);

  const float inf = std::numeric_limits<float>::infinity();
  float t_max_x = inf;
  float t_max_y = inf;
  float t_max_z = inf;
  float t_delta_x = inf;
  float t_delta_y = inf;
  float t_delta_z = inf;

  if (step_x != 0) {
    const float next = (step_x > 0) ? static_cast<float>(current.x + 1)
                                    : static_cast<float>(current.x);
    t_max_x = (next - origin.x) / direction.x;
    t_delta_x = 1.0f / std::abs(direction.x);
  }
  if (step_y != 0) {
    const float next = (step_y > 0) ? static_cast<float>(current.y + 1)
                                    : static_cast<float>(current.y);
    t_max_y = (next - origin.y) / direction.y;
    t_delta_y = 1.0f / std::abs(direction.y);
  }
  if (step_z != 0) {
    const float next = (step_z > 0) ? static_cast<float>(current.z + 1)
                                    : static_cast<float>(current.z);
    t_max_z = (next - origin.z) / direction.z;
    t_delta_z = 1.0f / std::abs(direction.z);
  }

  if (world.GetBlock(current.x, current.y, current.z) != BlockId::Air) {
    result.hit = true;
    result.block = current;
    result.previous = current;
    return result;
  }

  float distance = 0.0f;
  Vector3 hit_normal{0.0f, 0.0f, 0.0f};

  while (distance <= max_distance) {
    if (t_max_x < t_max_y) {
      if (t_max_x < t_max_z) {
        previous = current;
        current.x += step_x;
        distance = t_max_x;
        t_max_x += t_delta_x;
        hit_normal = {-static_cast<float>(step_x), 0.0f, 0.0f};
      } else {
        previous = current;
        current.z += step_z;
        distance = t_max_z;
        t_max_z += t_delta_z;
        hit_normal = {0.0f, 0.0f, -static_cast<float>(step_z)};
      }
    } else {
      if (t_max_y < t_max_z) {
        previous = current;
        current.y += step_y;
        distance = t_max_y;
        t_max_y += t_delta_y;
        hit_normal = {0.0f, -static_cast<float>(step_y), 0.0f};
      } else {
        previous = current;
        current.z += step_z;
        distance = t_max_z;
        t_max_z += t_delta_z;
        hit_normal = {0.0f, 0.0f, -static_cast<float>(step_z)};
      }
    }

    if (distance > max_distance) {
      break;
    }

    if (world.GetBlock(current.x, current.y, current.z) != BlockId::Air) {
      result.hit = true;
      result.block = current;
      result.previous = previous;
      result.normal = hit_normal;
      return result;
    }
  }

  return result;
}

Vector3 BlockCenter(const Int3& block) {
  return {(block.x + 0.5f) * kBlockSize, (block.y + 0.5f) * kBlockSize,
          (block.z + 0.5f) * kBlockSize};
}
}  // namespace

int main() {
  SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);
  InitWindow(kScreenWidth, kScreenHeight, "Minecraft Clone - Prototype");
  DisableCursor();

  Camera3D camera{};
  camera.position = {12.0f, 8.0f, 12.0f};
  camera.target = {0.0f, 2.0f, 0.0f};
  camera.up = {0.0f, 1.0f, 0.0f};
  camera.fovy = 60.0f;
  camera.projection = CAMERA_PERSPECTIVE;

  World world;
  world.GenerateFlat(kWorldRadiusChunks);

  SetTargetFPS(60);

  while (!WindowShouldClose()) {
    UpdateCamera(&camera, CAMERA_FIRST_PERSON);

    const Vector2 screen_center{GetScreenWidth() * 0.5f, GetScreenHeight() * 0.5f};
    const Ray ray = GetMouseRay(screen_center, camera);
    const RayHit hit = RaycastVoxels(world, ray.position, ray.direction, kRaycastDistance);

    bool changed = false;
    if (hit.hit && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
      changed = changed ||
                world.SetBlock(hit.block.x, hit.block.y, hit.block.z, BlockId::Air);
    }
    if (hit.hit && IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
      if (world.GetBlock(hit.previous.x, hit.previous.y, hit.previous.z) == BlockId::Air) {
        changed = changed || world.SetBlock(hit.previous.x, hit.previous.y,
                                            hit.previous.z, BlockId::Dirt);
      }
    }

    if (changed) {
      world.RebuildDirtyMeshes();
    }

    BeginDrawing();
    ClearBackground({135, 206, 235, 255});

    BeginMode3D(camera);
    world.Draw();
    DrawGrid(32, 1.0f);
    if (hit.hit) {
      const Vector3 center = BlockCenter(hit.block);
      DrawCubeWires(center, kBlockSize, kBlockSize, kBlockSize, YELLOW);
    }
    EndMode3D();

    DrawRectangleLinesEx({10.0f, 10.0f, 420.0f, 90.0f}, 1.0f, Fade(BLACK, 0.2f));
    DrawText("WASD + mouse to move", 20, 20, 18, BLACK);
    DrawText("Space/Shift to move up/down", 20, 42, 18, BLACK);
    DrawText("LMB: break  RMB: place", 20, 64, 18, BLACK);
    DrawCircleLines(static_cast<int>(screen_center.x), static_cast<int>(screen_center.y), 4,
                    Fade(BLACK, 0.6f));

    EndDrawing();
  }

  world.Clear();
  CloseWindow();
  return 0;
}
