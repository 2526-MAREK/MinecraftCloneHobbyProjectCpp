#pragma once

#include <DirectXMath.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

constexpr int kChunkSize = 16;
constexpr int kChunkVolume = kChunkSize * kChunkSize * kChunkSize;
constexpr int kGroundHeight = 2;
constexpr float kBlockSize = 1.0f;
constexpr int kAtlasTilesX = 4;
constexpr int kAtlasTilesY = 1;
constexpr int kTileGrassTop = 0;
constexpr int kTileGrassSide = 1;
constexpr int kTileDirt = 2;
constexpr int kTileStone = 3;
constexpr float kRaycastDistance = 8.0f;
constexpr int kWorldRadiusChunks = 3;
constexpr int kWorldMinChunkY = 0;
constexpr int kWorldMaxChunkY = 0;

struct Vertex {
  DirectX::XMFLOAT3 position;
  DirectX::XMFLOAT4 color;
  DirectX::XMFLOAT2 uv;
};

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

bool operator==(const Int3& lhs, const Int3& rhs);

struct Int3Hash {
  size_t operator()(const Int3& value) const noexcept;
};

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
  DirectX::XMFLOAT3 normal;
  std::array<DirectX::XMFLOAT3, 4> corners;
  float shade;
  FaceDir dir;
};

extern const std::array<FaceDef, 6> kFaces;

struct VoxelChunk {
  std::vector<BlockId> blocks;

  VoxelChunk();
  BlockId Get(int x, int y, int z) const;
  void Set(int x, int y, int z, BlockId id);
};

struct Chunk {
  Int3 coord{0, 0, 0};
  VoxelChunk voxels;
  bool dirty = true;
};

struct World {
  std::unordered_map<Int3, Chunk, Int3Hash> chunks;
};

struct RayHit {
  bool hit = false;
  Int3 block{0, 0, 0};
  Int3 previous{0, 0, 0};
};

int FloorDiv(int value, int divisor);
int Mod(int value, int divisor);
Int3 WorldToChunkCoord(int x, int y, int z);
Int3 WorldToLocalCoord(int x, int y, int z);
Int3 WorldBlockFromPosition(const DirectX::XMFLOAT3& position);

Chunk* FindChunk(World& world, const Int3& coord);
const Chunk* FindChunk(const World& world, const Int3& coord);
void MarkChunkDirty(World& world, const Int3& coord);
void MarkNeighborChunksDirty(World& world, const Int3& coord);
BlockId GetBlock(const World& world, int x, int y, int z);
bool SetBlock(World& world, int x, int y, int z, BlockId id);

RayHit RaycastVoxel(const World& world, const DirectX::XMFLOAT3& origin,
                    const DirectX::XMFLOAT3& direction, float max_distance);
bool HandleBlockInteraction(World& world, const RayHit& hit, bool lmb_pressed,
                            bool rmb_pressed);

void GenerateFlatChunk(VoxelChunk& chunk);
Chunk& GetOrCreateChunk(World& world, const Int3& coord);
void RemoveChunk(World& world, const Int3& coord);
void StreamChunks(World& world, const DirectX::XMFLOAT3& camera_position);

DirectX::XMFLOAT4 ApplyShade(const DirectX::XMFLOAT4& color, float shade);
int GetTileIndex(BlockId id, FaceDir dir);
std::array<DirectX::XMFLOAT2, 4> GetTileUVs(int tile_index);
void AddFace(std::vector<Vertex>& vertices, const DirectX::XMFLOAT3& base,
             const FaceDef& face, const DirectX::XMFLOAT4& color,
             int tile_index);
void AddFaceScaled(std::vector<Vertex>& vertices, const DirectX::XMFLOAT3& base,
                   float scale, const FaceDef& face,
                   const DirectX::XMFLOAT4& color, int tile_index);

std::vector<Vertex> BuildVoxelMesh(const World& world, const Chunk& chunk);
