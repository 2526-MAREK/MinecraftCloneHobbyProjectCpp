#include "world.h"

#include <cmath>
#include <limits>
#include <utility>

bool operator==(const Int3& lhs, const Int3& rhs) {
  return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
}

size_t Int3Hash::operator()(const Int3& value) const noexcept {
  const size_t hx = static_cast<size_t>(value.x) * 73856093u;
  const size_t hy = static_cast<size_t>(value.y) * 19349663u;
  const size_t hz = static_cast<size_t>(value.z) * 83492791u;
  return hx ^ hy ^ hz;
}

const std::array<FaceDef, 6> kFaces = {{
    {{1, 0, 0}, {1.0f, 0.0f, 0.0f},
     {{{1.0f, 0.0f, 0.0f},
       {1.0f, 1.0f, 0.0f},
       {1.0f, 1.0f, 1.0f},
       {1.0f, 0.0f, 1.0f}}},
     0.85f, FaceDir::PosX},
    {{-1, 0, 0}, {-1.0f, 0.0f, 0.0f},
     {{{0.0f, 0.0f, 1.0f},
       {0.0f, 1.0f, 1.0f},
       {0.0f, 1.0f, 0.0f},
       {0.0f, 0.0f, 0.0f}}},
     0.85f, FaceDir::NegX},
    {{0, 1, 0}, {0.0f, 1.0f, 0.0f},
     {{{0.0f, 1.0f, 0.0f},
       {0.0f, 1.0f, 1.0f},
       {1.0f, 1.0f, 1.0f},
       {1.0f, 1.0f, 0.0f}}},
     1.0f, FaceDir::PosY},
    {{0, -1, 0}, {0.0f, -1.0f, 0.0f},
     {{{0.0f, 0.0f, 1.0f},
       {0.0f, 0.0f, 0.0f},
       {1.0f, 0.0f, 0.0f},
       {1.0f, 0.0f, 1.0f}}},
     0.6f, FaceDir::NegY},
    {{0, 0, 1}, {0.0f, 0.0f, 1.0f},
     {{{0.0f, 0.0f, 1.0f},
       {1.0f, 0.0f, 1.0f},
       {1.0f, 1.0f, 1.0f},
       {0.0f, 1.0f, 1.0f}}},
     0.85f, FaceDir::PosZ},
    {{0, 0, -1}, {0.0f, 0.0f, -1.0f},
     {{{1.0f, 0.0f, 0.0f},
       {0.0f, 0.0f, 0.0f},
       {0.0f, 1.0f, 0.0f},
       {1.0f, 1.0f, 0.0f}}},
     0.85f, FaceDir::NegZ},
}};

VoxelChunk::VoxelChunk() : blocks(kChunkVolume, BlockId::Air) {}

BlockId VoxelChunk::Get(int x, int y, int z) const {
  const int index = x + (y * kChunkSize) + (z * kChunkSize * kChunkSize);
  return blocks[static_cast<size_t>(index)];
}

void VoxelChunk::Set(int x, int y, int z, BlockId id) {
  const int index = x + (y * kChunkSize) + (z * kChunkSize * kChunkSize);
  blocks[static_cast<size_t>(index)] = id;
}

int FloorDiv(int value, int divisor) {
  int quotient = value / divisor;
  int remainder = value % divisor;
  if (remainder != 0 && ((remainder < 0) != (divisor < 0))) {
    --quotient;
  }
  return quotient;
}

int Mod(int value, int divisor) {
  int result = value % divisor;
  if (result < 0) {
    result += divisor;
  }
  return result;
}

Int3 WorldToChunkCoord(int x, int y, int z) {
  return {FloorDiv(x, kChunkSize), FloorDiv(y, kChunkSize),
          FloorDiv(z, kChunkSize)};
}

Int3 WorldToLocalCoord(int x, int y, int z) {
  return {Mod(x, kChunkSize), Mod(y, kChunkSize), Mod(z, kChunkSize)};
}

Int3 WorldBlockFromPosition(const DirectX::XMFLOAT3& position) {
  return {static_cast<int>(std::floor(position.x)),
          static_cast<int>(std::floor(position.y)),
          static_cast<int>(std::floor(position.z))};
}

Chunk* FindChunk(World& world, const Int3& coord) {
  auto it = world.chunks.find(coord);
  if (it == world.chunks.end()) {
    return nullptr;
  }
  return &it->second;
}

const Chunk* FindChunk(const World& world, const Int3& coord) {
  auto it = world.chunks.find(coord);
  if (it == world.chunks.end()) {
    return nullptr;
  }
  return &it->second;
}

void MarkChunkDirty(World& world, const Int3& coord) {
  Chunk* chunk = FindChunk(world, coord);
  if (chunk) {
    chunk->dirty = true;
  }
}

void MarkNeighborChunksDirty(World& world, const Int3& coord) {
  MarkChunkDirty(world, {coord.x + 1, coord.y, coord.z});
  MarkChunkDirty(world, {coord.x - 1, coord.y, coord.z});
  MarkChunkDirty(world, {coord.x, coord.y + 1, coord.z});
  MarkChunkDirty(world, {coord.x, coord.y - 1, coord.z});
  MarkChunkDirty(world, {coord.x, coord.y, coord.z + 1});
  MarkChunkDirty(world, {coord.x, coord.y, coord.z - 1});
}

BlockId GetBlock(const World& world, int x, int y, int z) {
  const Int3 chunk_coord = WorldToChunkCoord(x, y, z);
  const Chunk* chunk = FindChunk(world, chunk_coord);
  if (!chunk) {
    return BlockId::Air;
  }
  const Int3 local = WorldToLocalCoord(x, y, z);
  return chunk->voxels.Get(local.x, local.y, local.z);
}

bool SetBlock(World& world, int x, int y, int z, BlockId id) {
  const Int3 chunk_coord = WorldToChunkCoord(x, y, z);
  Chunk* chunk = FindChunk(world, chunk_coord);
  if (!chunk) {
    return false;
  }
  const Int3 local = WorldToLocalCoord(x, y, z);
  if (chunk->voxels.Get(local.x, local.y, local.z) == id) {
    return false;
  }
  chunk->voxels.Set(local.x, local.y, local.z, id);
  chunk->dirty = true;
  if (local.x == 0) {
    MarkChunkDirty(world, {chunk_coord.x - 1, chunk_coord.y, chunk_coord.z});
  } else if (local.x == kChunkSize - 1) {
    MarkChunkDirty(world, {chunk_coord.x + 1, chunk_coord.y, chunk_coord.z});
  }
  if (local.y == 0) {
    MarkChunkDirty(world, {chunk_coord.x, chunk_coord.y - 1, chunk_coord.z});
  } else if (local.y == kChunkSize - 1) {
    MarkChunkDirty(world, {chunk_coord.x, chunk_coord.y + 1, chunk_coord.z});
  }
  if (local.z == 0) {
    MarkChunkDirty(world, {chunk_coord.x, chunk_coord.y, chunk_coord.z - 1});
  } else if (local.z == kChunkSize - 1) {
    MarkChunkDirty(world, {chunk_coord.x, chunk_coord.y, chunk_coord.z + 1});
  }
  return true;
}

RayHit RaycastVoxel(const World& world, const DirectX::XMFLOAT3& origin,
                    const DirectX::XMFLOAT3& direction, float max_distance) {
  RayHit result;

  float dx = direction.x;
  float dy = direction.y;
  float dz = direction.z;
  const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
  if (len <= 0.0f) {
    return result;
  }
  dx /= len;
  dy /= len;
  dz /= len;

  const float ox = origin.x;
  const float oy = origin.y;
  const float oz = origin.z;

  int x = static_cast<int>(std::floor(ox));
  int y = static_cast<int>(std::floor(oy));
  int z = static_cast<int>(std::floor(oz));
  Int3 current{x, y, z};
  Int3 previous = current;

  const int step_x = (dx > 0.0f) ? 1 : (dx < 0.0f ? -1 : 0);
  const int step_y = (dy > 0.0f) ? 1 : (dy < 0.0f ? -1 : 0);
  const int step_z = (dz > 0.0f) ? 1 : (dz < 0.0f ? -1 : 0);

  const float inf = std::numeric_limits<float>::infinity();
  float t_max_x = inf;
  float t_max_y = inf;
  float t_max_z = inf;
  float t_delta_x = inf;
  float t_delta_y = inf;
  float t_delta_z = inf;

  if (step_x != 0) {
    const float next = (step_x > 0) ? (static_cast<float>(x + 1) - ox)
                                    : (ox - static_cast<float>(x));
    t_max_x = next / std::abs(dx);
    t_delta_x = 1.0f / std::abs(dx);
  }
  if (step_y != 0) {
    const float next = (step_y > 0) ? (static_cast<float>(y + 1) - oy)
                                    : (oy - static_cast<float>(y));
    t_max_y = next / std::abs(dy);
    t_delta_y = 1.0f / std::abs(dy);
  }
  if (step_z != 0) {
    const float next = (step_z > 0) ? (static_cast<float>(z + 1) - oz)
                                    : (oz - static_cast<float>(z));
    t_max_z = next / std::abs(dz);
    t_delta_z = 1.0f / std::abs(dz);
  }

  if (GetBlock(world, current.x, current.y, current.z) != BlockId::Air) {
    result.hit = true;
    result.block = current;
    result.previous = current;
    return result;
  }

  float distance = 0.0f;
  while (distance <= max_distance) {
    if (t_max_x < t_max_y) {
      if (t_max_x < t_max_z) {
        previous = current;
        current.x += step_x;
        distance = t_max_x;
        t_max_x += t_delta_x;
      } else {
        previous = current;
        current.z += step_z;
        distance = t_max_z;
        t_max_z += t_delta_z;
      }
    } else {
      if (t_max_y < t_max_z) {
        previous = current;
        current.y += step_y;
        distance = t_max_y;
        t_max_y += t_delta_y;
      } else {
        previous = current;
        current.z += step_z;
        distance = t_max_z;
        t_max_z += t_delta_z;
      }
    }

    if (distance > max_distance) {
      break;
    }

    if (GetBlock(world, current.x, current.y, current.z) != BlockId::Air) {
      result.hit = true;
      result.block = current;
      result.previous = previous;
      return result;
    }
  }

  return result;
}

bool HandleBlockInteraction(World& world, const RayHit& hit, bool lmb_pressed,
                            bool rmb_pressed) {
  bool changed = false;
  if (lmb_pressed) {
    changed = SetBlock(world, hit.block.x, hit.block.y, hit.block.z,
                       BlockId::Air);
  }
  if (rmb_pressed) {
    if (GetBlock(world, hit.previous.x, hit.previous.y, hit.previous.z) ==
        BlockId::Air) {
      changed = SetBlock(world, hit.previous.x, hit.previous.y, hit.previous.z,
                         BlockId::Dirt) ||
                changed;
    }
  }
  return changed;
}

void GenerateFlatChunk(VoxelChunk& chunk) {
  for (int z = 0; z < kChunkSize; ++z) {
    for (int x = 0; x < kChunkSize; ++x) {
      for (int y = 0; y < kGroundHeight; ++y) {
        const BlockId id =
            (y == kGroundHeight - 1) ? BlockId::Grass : BlockId::Dirt;
        chunk.Set(x, y, z, id);
      }
    }
  }
}

Chunk& GetOrCreateChunk(World& world, const Int3& coord) {
  auto it = world.chunks.find(coord);
  if (it != world.chunks.end()) {
    return it->second;
  }
  Chunk chunk;
  chunk.coord = coord;
  GenerateFlatChunk(chunk.voxels);
  chunk.dirty = true;
  auto inserted = world.chunks.emplace(coord, std::move(chunk));
  MarkNeighborChunksDirty(world, coord);
  return inserted.first->second;
}

void RemoveChunk(World& world, const Int3& coord) {
  auto it = world.chunks.find(coord);
  if (it == world.chunks.end()) {
    return;
  }
  MarkNeighborChunksDirty(world, coord);
  world.chunks.erase(it);
}

void StreamChunks(World& world, const DirectX::XMFLOAT3& camera_position) {
  const Int3 camera_block = WorldBlockFromPosition(camera_position);
  const Int3 center =
      WorldToChunkCoord(camera_block.x, camera_block.y, camera_block.z);

  for (int cy = kWorldMinChunkY; cy <= kWorldMaxChunkY; ++cy) {
    for (int dz = -kWorldRadiusChunks; dz <= kWorldRadiusChunks; ++dz) {
      for (int dx = -kWorldRadiusChunks; dx <= kWorldRadiusChunks; ++dx) {
        const Int3 coord{center.x + dx, cy, center.z + dz};
        GetOrCreateChunk(world, coord);
      }
    }
  }

  std::vector<Int3> to_remove;
  for (const auto& entry : world.chunks) {
    const Int3& coord = entry.first;
    if (coord.x < center.x - kWorldRadiusChunks ||
        coord.x > center.x + kWorldRadiusChunks ||
        coord.z < center.z - kWorldRadiusChunks ||
        coord.z > center.z + kWorldRadiusChunks ||
        coord.y < kWorldMinChunkY || coord.y > kWorldMaxChunkY) {
      to_remove.push_back(coord);
    }
  }
  for (const Int3& coord : to_remove) {
    RemoveChunk(world, coord);
  }
}

DirectX::XMFLOAT4 ApplyShade(const DirectX::XMFLOAT4& color, float shade) {
  return {color.x * shade, color.y * shade, color.z * shade, color.w};
}

int GetTileIndex(BlockId id, FaceDir dir) {
  switch (id) {
    case BlockId::Grass:
      if (dir == FaceDir::PosY) {
        return kTileGrassTop;
      }
      if (dir == FaceDir::NegY) {
        return kTileDirt;
      }
      return kTileGrassSide;
    case BlockId::Dirt:
      return kTileDirt;
    case BlockId::Stone:
      return kTileStone;
    case BlockId::Air:
      break;
  }
  return kTileDirt;
}

std::array<DirectX::XMFLOAT2, 4> GetTileUVs(int tile_index) {
  const float tile_w = 1.0f / static_cast<float>(kAtlasTilesX);
  const float tile_h = 1.0f / static_cast<float>(kAtlasTilesY);
  const int tile_x = tile_index % kAtlasTilesX;
  const int tile_y = tile_index / kAtlasTilesX;
  const float u0 = tile_x * tile_w;
  const float v0 = tile_y * tile_h;
  const float u1 = u0 + tile_w;
  const float v1 = v0 + tile_h;
  return {{
      {u0, v0},
      {u1, v0},
      {u1, v1},
      {u0, v1},
  }};
}

void AddFace(std::vector<Vertex>& vertices, const DirectX::XMFLOAT3& base,
             const FaceDef& face, const DirectX::XMFLOAT4& color,
             int tile_index) {
  constexpr int indices[6] = {0, 1, 2, 0, 2, 3};
  const std::array<DirectX::XMFLOAT2, 4> uvs = GetTileUVs(tile_index);
  for (int i = 0; i < 6; ++i) {
    const int idx = indices[i];
    const DirectX::XMFLOAT3& corner = face.corners[static_cast<size_t>(idx)];
    Vertex vertex;
    vertex.position = {
        base.x + corner.x * kBlockSize,
        base.y + corner.y * kBlockSize,
        base.z + corner.z * kBlockSize,
    };
    vertex.color = color;
    vertex.uv = uvs[static_cast<size_t>(idx)];
    vertices.push_back(vertex);
  }
}

void AddFaceScaled(std::vector<Vertex>& vertices, const DirectX::XMFLOAT3& base,
                   float scale, const FaceDef& face,
                   const DirectX::XMFLOAT4& color, int tile_index) {
  constexpr int indices[6] = {0, 1, 2, 0, 2, 3};
  const std::array<DirectX::XMFLOAT2, 4> uvs = GetTileUVs(tile_index);
  const float size = kBlockSize * scale;
  for (int i = 0; i < 6; ++i) {
    const int idx = indices[i];
    const DirectX::XMFLOAT3& corner = face.corners[static_cast<size_t>(idx)];
    Vertex vertex;
    vertex.position = {
        base.x + corner.x * size,
        base.y + corner.y * size,
        base.z + corner.z * size,
    };
    vertex.color = color;
    vertex.uv = uvs[static_cast<size_t>(idx)];
    vertices.push_back(vertex);
  }
}

std::vector<Vertex> BuildVoxelMesh(const World& world, const Chunk& chunk) {
  std::vector<Vertex> vertices;
  vertices.reserve(static_cast<size_t>(kChunkVolume) * 36u);
  const int base_x = chunk.coord.x * kChunkSize;
  const int base_y = chunk.coord.y * kChunkSize;
  const int base_z = chunk.coord.z * kChunkSize;
  for (int z = 0; z < kChunkSize; ++z) {
    for (int y = 0; y < kChunkSize; ++y) {
      for (int x = 0; x < kChunkSize; ++x) {
        const BlockId id = chunk.voxels.Get(x, y, z);
        if (id == BlockId::Air) {
          continue;
        }
        const int world_x = base_x + x;
        const int world_y = base_y + y;
        const int world_z = base_z + z;
        for (const auto& face : kFaces) {
          const BlockId neighbor =
              GetBlock(world, world_x + face.neighbor.x,
                       world_y + face.neighbor.y, world_z + face.neighbor.z);
          if (neighbor != BlockId::Air) {
            continue;
          }
          const DirectX::XMFLOAT3 base{
              world_x * kBlockSize,
              world_y * kBlockSize,
              world_z * kBlockSize,
          };
          const DirectX::XMFLOAT4 base_color{1.0f, 1.0f, 1.0f, 1.0f};
          const DirectX::XMFLOAT4 shaded = ApplyShade(base_color, face.shade);
          const int tile_index = GetTileIndex(id, face.dir);
          AddFace(vertices, base, face, shaded, tile_index);
        }
      }
    }
  }
  return vertices;
}
