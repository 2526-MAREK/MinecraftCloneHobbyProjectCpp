#include <windows.h>

#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <DirectXMath.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

namespace {
constexpr int kInitialWidth = 1280;
constexpr int kInitialHeight = 720;
constexpr wchar_t kWindowClassName[] = L"MinecraftCloneDX11Window";
constexpr wchar_t kWindowTitle[] = L"Minecraft Clone - DirectX 11";
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
constexpr float kMoveSpeed = 6.0f;
constexpr float kMouseSensitivity = 0.002f;
constexpr float kMaxPitch = DirectX::XM_PIDIV2 - 0.01f;
constexpr float kRaycastDistance = 8.0f;
constexpr float kSelectionScale = 1.03f;
constexpr float kHudScale = 2.0f;
constexpr float kHudPadding = 12.0f;
constexpr float kCrosshairLength = 10.0f;
constexpr float kCrosshairGap = 6.0f;
constexpr float kCrosshairThickness = 2.0f;
constexpr int kWorldRadiusChunks = 3;
constexpr int kWorldMinChunkY = 0;
constexpr int kWorldMaxChunkY = 0;

using D3DCompileFn =
    HRESULT(WINAPI*)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*,
                     LPCSTR, LPCSTR, UINT, UINT, ID3DBlob**, ID3DBlob**);

HMODULE g_d3dcompiler_module = nullptr;
D3DCompileFn g_d3d_compile = nullptr;

struct D3DState {
  HWND hwnd = nullptr;
  UINT width = 0;
  UINT height = 0;
  Microsoft::WRL::ComPtr<ID3D11Device> device;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
  Microsoft::WRL::ComPtr<IDXGISwapChain> swap_chain;
  Microsoft::WRL::ComPtr<ID3D11RenderTargetView> render_target;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> depth_buffer;
  Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depth_stencil_view;
  Microsoft::WRL::ComPtr<ID3D11VertexShader> vertex_shader;
  Microsoft::WRL::ComPtr<ID3D11PixelShader> pixel_shader;
  Microsoft::WRL::ComPtr<ID3D11InputLayout> input_layout;
  Microsoft::WRL::ComPtr<ID3D11Buffer> constant_buffer;
  Microsoft::WRL::ComPtr<ID3D11RasterizerState> rasterizer_state;
  Microsoft::WRL::ComPtr<ID3D11PixelShader> solid_pixel_shader;
  Microsoft::WRL::ComPtr<ID3D11Buffer> highlight_vertex_buffer;
  Microsoft::WRL::ComPtr<ID3D11RasterizerState> wireframe_state;
  Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depth_state;
  Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depth_state_no_depth;
  Microsoft::WRL::ComPtr<ID3D11Buffer> hud_vertex_buffer;
  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> texture_srv;
  Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler_state;
  UINT vertex_stride = 0;
  UINT vertex_offset = 0;
  UINT highlight_vertex_count = 0;
  UINT highlight_vertex_buffer_size = 0;
  UINT hud_vertex_count = 0;
  UINT hud_vertex_buffer_size = 0;
};

struct CameraState {
  DirectX::XMFLOAT3 position;
  float yaw;
  float pitch;
  float move_speed;
  float mouse_sensitivity;
};

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

bool operator==(const Int3& lhs, const Int3& rhs) {
  return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
}

struct Int3Hash {
  size_t operator()(const Int3& value) const noexcept {
    const size_t hx = static_cast<size_t>(value.x) * 73856093u;
    const size_t hy = static_cast<size_t>(value.y) * 19349663u;
    const size_t hz = static_cast<size_t>(value.z) * 83492791u;
    return hx ^ hy ^ hz;
  }
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

struct VoxelChunk {
  std::vector<BlockId> blocks;

  VoxelChunk() : blocks(kChunkVolume, BlockId::Air) {}

  BlockId Get(int x, int y, int z) const {
    const int index = x + (y * kChunkSize) + (z * kChunkSize * kChunkSize);
    return blocks[static_cast<size_t>(index)];
  }

  void Set(int x, int y, int z, BlockId id) {
    const int index = x + (y * kChunkSize) + (z * kChunkSize * kChunkSize);
    blocks[static_cast<size_t>(index)] = id;
  }
};

struct Chunk {
  Int3 coord{0, 0, 0};
  VoxelChunk voxels;
  Microsoft::WRL::ComPtr<ID3D11Buffer> vertex_buffer;
  UINT vertex_count = 0;
  UINT vertex_buffer_size = 0;
  bool dirty = true;
};

struct World {
  std::unordered_map<Int3, Chunk, Int3Hash> chunks;
};

std::vector<Vertex> BuildVoxelMesh(const World& world, const Chunk& chunk);
bool UploadChunkMesh(Chunk& chunk, const std::vector<Vertex>& vertices);
std::vector<Vertex> BuildSelectionMesh(const Int3& block);
bool UploadSelectionMesh(const std::vector<Vertex>& vertices);
std::vector<Vertex> BuildHudMesh();
bool UploadHudMesh(const std::vector<Vertex>& vertices);

D3DState g_d3d;
World g_world;
CameraState g_camera = {{8.0f, 6.0f, -14.0f}, 0.0f, 0.0f, kMoveSpeed,
                         kMouseSensitivity};
bool g_mouse_captured = false;
bool g_escape_down = false;
bool g_lmb_down = false;
bool g_rmb_down = false;
float g_fps = 0.0f;
float g_fps_timer = 0.0f;
int g_fps_samples = 0;

void ShowError(const char* message, HRESULT hr) {
  char buffer[256];
  std::snprintf(buffer, sizeof(buffer), "%s (HRESULT 0x%08X)", message,
                static_cast<unsigned int>(hr));
  MessageBoxA(nullptr, buffer, "DirectX 11 Error", MB_ICONERROR);
}

bool LoadD3DCompiler() {
  if (g_d3d_compile) {
    return true;
  }
  g_d3dcompiler_module = LoadLibraryW(L"d3dcompiler_47.dll");
  if (!g_d3dcompiler_module) {
    MessageBoxA(nullptr,
                "Failed to load d3dcompiler_47.dll.\n"
                "Install the Windows 10/11 SDK or DirectX runtime.",
                "DirectX 11 Error", MB_ICONERROR);
    return false;
  }
  g_d3d_compile = reinterpret_cast<D3DCompileFn>(
      GetProcAddress(g_d3dcompiler_module, "D3DCompile"));
  if (!g_d3d_compile) {
    MessageBoxA(nullptr, "Failed to find D3DCompile in d3dcompiler_47.dll.",
                "DirectX 11 Error", MB_ICONERROR);
    return false;
  }
  return true;
}

void SetCursorVisible(bool visible) {
  if (visible) {
    while (ShowCursor(TRUE) < 0) {
    }
  } else {
    while (ShowCursor(FALSE) >= 0) {
    }
  }
}

POINT GetClientCenter() {
  POINT center{0, 0};
  if (!g_d3d.hwnd) {
    return center;
  }
  RECT rect{};
  GetClientRect(g_d3d.hwnd, &rect);
  center.x = (rect.right - rect.left) / 2;
  center.y = (rect.bottom - rect.top) / 2;
  return center;
}

void CenterCursor() {
  if (!g_d3d.hwnd) {
    return;
  }
  POINT center = GetClientCenter();
  ClientToScreen(g_d3d.hwnd, &center);
  SetCursorPos(center.x, center.y);
}

void UpdateClipRect() {
  if (!g_mouse_captured || !g_d3d.hwnd) {
    return;
  }
  RECT rect{};
  GetClientRect(g_d3d.hwnd, &rect);
  POINT tl{rect.left, rect.top};
  POINT br{rect.right, rect.bottom};
  ClientToScreen(g_d3d.hwnd, &tl);
  ClientToScreen(g_d3d.hwnd, &br);
  RECT clip{tl.x, tl.y, br.x, br.y};
  ClipCursor(&clip);
}

void SetMouseCaptured(bool captured) {
  if (captured == g_mouse_captured) {
    return;
  }
  g_mouse_captured = captured;
  if (captured) {
    SetCursorVisible(false);
    UpdateClipRect();
    CenterCursor();
  } else {
    ClipCursor(nullptr);
    SetCursorVisible(true);
  }
}

DirectX::XMVECTOR GetCameraForward() {
  const float cos_pitch = std::cos(g_camera.pitch);
  const float sin_pitch = std::sin(g_camera.pitch);
  const float sin_yaw = std::sin(g_camera.yaw);
  const float cos_yaw = std::cos(g_camera.yaw);
  return DirectX::XMVector3Normalize(
      DirectX::XMVectorSet(cos_pitch * sin_yaw, sin_pitch, cos_pitch * cos_yaw,
                           0.0f));
}

void UpdateCamera(float dt) {
  if (!g_mouse_captured || !g_d3d.hwnd) {
    return;
  }

  POINT center = GetClientCenter();
  ClientToScreen(g_d3d.hwnd, &center);
  POINT cursor{};
  GetCursorPos(&cursor);
  const int dx = cursor.x - center.x;
  const int dy = cursor.y - center.y;
  if (dx != 0 || dy != 0) {
    g_camera.yaw += static_cast<float>(dx) * g_camera.mouse_sensitivity;
    g_camera.pitch -= static_cast<float>(dy) * g_camera.mouse_sensitivity;
    g_camera.pitch = std::clamp(g_camera.pitch, -kMaxPitch, kMaxPitch);
  }
  CenterCursor();

  const DirectX::XMVECTOR forward = GetCameraForward();
  const DirectX::XMVECTOR up = DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
  const DirectX::XMVECTOR right =
      DirectX::XMVector3Normalize(DirectX::XMVector3Cross(up, forward));

  DirectX::XMVECTOR move = DirectX::XMVectorZero();
  if (GetAsyncKeyState('W') & 0x8000) {
    move = DirectX::XMVectorAdd(move, forward);
  }
  if (GetAsyncKeyState('S') & 0x8000) {
    move = DirectX::XMVectorSubtract(move, forward);
  }
  if (GetAsyncKeyState('A') & 0x8000) {
    move = DirectX::XMVectorSubtract(move, right);
  }
  if (GetAsyncKeyState('D') & 0x8000) {
    move = DirectX::XMVectorAdd(move, right);
  }
  if (GetAsyncKeyState(VK_SPACE) & 0x8000) {
    move = DirectX::XMVectorAdd(move, up);
  }
  if (GetAsyncKeyState(VK_SHIFT) & 0x8000) {
    move = DirectX::XMVectorSubtract(move, up);
  }

  const float speed =
      (GetAsyncKeyState(VK_CONTROL) & 0x8000) ? (g_camera.move_speed * 3.0f)
                                              : g_camera.move_speed;
  const float move_len_sq =
      DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(move));
  if (move_len_sq > 0.0001f) {
    move = DirectX::XMVector3Normalize(move);
    DirectX::XMVECTOR pos = DirectX::XMLoadFloat3(&g_camera.position);
    pos = DirectX::XMVectorAdd(pos, DirectX::XMVectorScale(move, speed * dt));
    DirectX::XMStoreFloat3(&g_camera.position, pos);
  }
}

void UpdateFps(float dt) {
  g_fps_timer += dt;
  ++g_fps_samples;
  if (g_fps_timer >= 0.25f) {
    g_fps = static_cast<float>(g_fps_samples) / g_fps_timer;
    g_fps_timer = 0.0f;
    g_fps_samples = 0;
  }
}

void UpdateInput(float dt) {
  const bool esc_down = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
  if (esc_down && !g_escape_down) {
    SetMouseCaptured(!g_mouse_captured);
  }
  g_escape_down = esc_down;

  if (g_mouse_captured) {
    UpdateCamera(dt);
  }
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

struct RayHit {
  bool hit = false;
  Int3 block{0, 0, 0};
  Int3 previous{0, 0, 0};
};

RayHit g_hover_hit;
bool g_hover_valid = false;

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

void UpdateHoverHit() {
  if (!g_mouse_captured) {
    g_hover_valid = false;
    return;
  }

  const DirectX::XMVECTOR forward_vec = GetCameraForward();
  DirectX::XMFLOAT3 forward{};
  DirectX::XMStoreFloat3(&forward, forward_vec);
  const DirectX::XMFLOAT3 origin = g_camera.position;
  const RayHit hit = RaycastVoxel(g_world, origin, forward, kRaycastDistance);
  g_hover_valid = hit.hit;
  if (g_hover_valid) {
    g_hover_hit = hit;
  }
}

void UpdateSelectionMesh() {
  if (!g_hover_valid) {
    g_d3d.highlight_vertex_count = 0;
    return;
  }
  const std::vector<Vertex> vertices = BuildSelectionMesh(g_hover_hit.block);
  UploadSelectionMesh(vertices);
}

bool HandleBlockInteraction() {
  const bool lmb = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
  const bool rmb = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
  const bool lmb_pressed = lmb && !g_lmb_down;
  const bool rmb_pressed = rmb && !g_rmb_down;
  g_lmb_down = lmb;
  g_rmb_down = rmb;

  if (!g_mouse_captured || !g_hover_valid || (!lmb_pressed && !rmb_pressed)) {
    return false;
  }
  const RayHit hit = g_hover_hit;

  bool changed = false;
  if (lmb_pressed) {
    changed =
        SetBlock(g_world, hit.block.x, hit.block.y, hit.block.z, BlockId::Air);
  }
  if (rmb_pressed) {
    if (GetBlock(g_world, hit.previous.x, hit.previous.y, hit.previous.z) ==
        BlockId::Air) {
      changed = SetBlock(g_world, hit.previous.x, hit.previous.y,
                         hit.previous.z, BlockId::Dirt) ||
                changed;
    }
  }

  return changed;
}

void GenerateFlatChunk(VoxelChunk& chunk) {
  for (int z = 0; z < kChunkSize; ++z) {
    for (int x = 0; x < kChunkSize; ++x) {
      for (int y = 0; y < kGroundHeight; ++y) {
        const BlockId id = (y == kGroundHeight - 1) ? BlockId::Grass : BlockId::Dirt;
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

void StreamChunks() {
  const Int3 camera_block = WorldBlockFromPosition(g_camera.position);
  const Int3 center = WorldToChunkCoord(camera_block.x, camera_block.y,
                                        camera_block.z);

  for (int cy = kWorldMinChunkY; cy <= kWorldMaxChunkY; ++cy) {
    for (int dz = -kWorldRadiusChunks; dz <= kWorldRadiusChunks; ++dz) {
      for (int dx = -kWorldRadiusChunks; dx <= kWorldRadiusChunks; ++dx) {
        const Int3 coord{center.x + dx, cy, center.z + dz};
        GetOrCreateChunk(g_world, coord);
      }
    }
  }

  std::vector<Int3> to_remove;
  for (const auto& entry : g_world.chunks) {
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
    RemoveChunk(g_world, coord);
  }
}

bool UpdateChunkMeshes() {
  for (auto& entry : g_world.chunks) {
    Chunk& chunk = entry.second;
    if (!chunk.dirty) {
      continue;
    }
    const std::vector<Vertex> vertices = BuildVoxelMesh(g_world, chunk);
    if (!UploadChunkMesh(chunk, vertices)) {
      return false;
    }
    chunk.dirty = false;
  }
  return true;
}

bool IsAabbVisible(const DirectX::XMMATRIX& view_proj,
                   const DirectX::XMFLOAT3& min_point,
                   const DirectX::XMFLOAT3& max_point) {
  DirectX::XMFLOAT3 corners[8] = {
      {min_point.x, min_point.y, min_point.z},
      {max_point.x, min_point.y, min_point.z},
      {min_point.x, max_point.y, min_point.z},
      {max_point.x, max_point.y, min_point.z},
      {min_point.x, min_point.y, max_point.z},
      {max_point.x, min_point.y, max_point.z},
      {min_point.x, max_point.y, max_point.z},
      {max_point.x, max_point.y, max_point.z},
  };

  int outside_left = 0;
  int outside_right = 0;
  int outside_bottom = 0;
  int outside_top = 0;
  int outside_near = 0;
  int outside_far = 0;

  for (const auto& corner : corners) {
    const DirectX::XMVECTOR pos =
        DirectX::XMVectorSet(corner.x, corner.y, corner.z, 1.0f);
    const DirectX::XMVECTOR clip = DirectX::XMVector4Transform(pos, view_proj);
    const float cx = DirectX::XMVectorGetX(clip);
    const float cy = DirectX::XMVectorGetY(clip);
    const float cz = DirectX::XMVectorGetZ(clip);
    const float cw = DirectX::XMVectorGetW(clip);

    if (cx < -cw) {
      ++outside_left;
    }
    if (cx > cw) {
      ++outside_right;
    }
    if (cy < -cw) {
      ++outside_bottom;
    }
    if (cy > cw) {
      ++outside_top;
    }
    if (cz < 0.0f) {
      ++outside_near;
    }
    if (cz > cw) {
      ++outside_far;
    }
  }

  if (outside_left == 8 || outside_right == 8 || outside_bottom == 8 ||
      outside_top == 8 || outside_near == 8 || outside_far == 8) {
    return false;
  }
  return true;
}

bool IsChunkVisible(const DirectX::XMMATRIX& view_proj, const Chunk& chunk) {
  const float size = static_cast<float>(kChunkSize) * kBlockSize;
  const DirectX::XMFLOAT3 min_point{
      chunk.coord.x * size,
      chunk.coord.y * size,
      chunk.coord.z * size,
  };
  const DirectX::XMFLOAT3 max_point{
      min_point.x + size,
      min_point.y + size,
      min_point.z + size,
  };
  return IsAabbVisible(view_proj, min_point, max_point);
}

DirectX::XMFLOAT4 ApplyShade(const DirectX::XMFLOAT4& color, float shade) {
  return {color.x * shade, color.y * shade, color.z * shade, color.w};
}

struct Glyph {
  char ch;
  std::array<uint8_t, 7> rows;
};

const std::array<Glyph, 17> kGlyphs = {{
    {'0', {0b01110, 0b10001, 0b10011, 0b10101, 0b11001, 0b10001, 0b01110}},
    {'1', {0b00100, 0b01100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110}},
    {'2', {0b01110, 0b10001, 0b00001, 0b00010, 0b00100, 0b01000, 0b11111}},
    {'3', {0b01110, 0b10001, 0b00001, 0b00110, 0b00001, 0b10001, 0b01110}},
    {'4', {0b00010, 0b00110, 0b01010, 0b10010, 0b11111, 0b00010, 0b00010}},
    {'5', {0b11111, 0b10000, 0b11110, 0b00001, 0b00001, 0b10001, 0b01110}},
    {'6', {0b00110, 0b01000, 0b10000, 0b11110, 0b10001, 0b10001, 0b01110}},
    {'7', {0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b01000, 0b01000}},
    {'8', {0b01110, 0b10001, 0b10001, 0b01110, 0b10001, 0b10001, 0b01110}},
    {'9', {0b01110, 0b10001, 0b10001, 0b01111, 0b00001, 0b00010, 0b01100}},
    {'F', {0b11111, 0b10000, 0b10000, 0b11110, 0b10000, 0b10000, 0b10000}},
    {'P', {0b11110, 0b10001, 0b10001, 0b11110, 0b10000, 0b10000, 0b10000}},
    {'S', {0b01111, 0b10000, 0b10000, 0b01110, 0b00001, 0b00001, 0b11110}},
    {'X', {0b10001, 0b01010, 0b00100, 0b00100, 0b00100, 0b01010, 0b10001}},
    {'Y', {0b10001, 0b01010, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100}},
    {'Z', {0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b10000, 0b11111}},
    {'B', {0b11110, 0b10001, 0b10001, 0b11110, 0b10001, 0b10001, 0b11110}},
}};

const Glyph* FindGlyph(char ch) {
  for (const auto& glyph : kGlyphs) {
    if (glyph.ch == ch) {
      return &glyph;
    }
  }
  return nullptr;
}

void AddQuadPixels(std::vector<Vertex>& vertices, float x, float y, float w,
                   float h, const DirectX::XMFLOAT4& color, float screen_w,
                   float screen_h) {
  const float x0 = (x / screen_w) * 2.0f - 1.0f;
  const float x1 = ((x + w) / screen_w) * 2.0f - 1.0f;
  const float y0 = 1.0f - (y / screen_h) * 2.0f;
  const float y1 = 1.0f - ((y + h) / screen_h) * 2.0f;

  const DirectX::XMFLOAT2 uv{0.0f, 0.0f};
  vertices.push_back({{x0, y0, 0.0f}, color, uv});
  vertices.push_back({{x1, y0, 0.0f}, color, uv});
  vertices.push_back({{x1, y1, 0.0f}, color, uv});
  vertices.push_back({{x0, y0, 0.0f}, color, uv});
  vertices.push_back({{x1, y1, 0.0f}, color, uv});
  vertices.push_back({{x0, y1, 0.0f}, color, uv});
}

void DrawText(std::vector<Vertex>& vertices, float x, float y, float scale,
              const char* text, const DirectX::XMFLOAT4& color, float screen_w,
              float screen_h) {
  const float advance = 6.0f * scale;
  const float pixel = scale;
  for (const char* ptr = text; *ptr; ++ptr) {
    const char ch = *ptr;
    if (ch == ' ') {
      x += advance;
      continue;
    }
    if (ch == ':' || ch == '-') {
      const bool is_colon = (ch == ':');
      for (int row = 0; row < 7; ++row) {
        bool on = false;
        if (is_colon) {
          on = (row == 1 || row == 2 || row == 4 || row == 5);
        } else {
          on = (row == 3);
        }
        if (on) {
          AddQuadPixels(vertices, x + 2.0f * pixel, y + row * pixel, pixel,
                        pixel, color, screen_w, screen_h);
        }
      }
      x += advance;
      continue;
    }

    const Glyph* glyph = FindGlyph(ch);
    if (!glyph) {
      x += advance;
      continue;
    }
    for (int row = 0; row < 7; ++row) {
      const uint8_t bits = glyph->rows[static_cast<size_t>(row)];
      for (int col = 0; col < 5; ++col) {
        if (bits & (1u << (4 - col))) {
          AddQuadPixels(vertices, x + col * pixel, y + row * pixel, pixel,
                        pixel, color, screen_w, screen_h);
        }
      }
    }
    x += advance;
  }
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
             const FaceDef& face, const DirectX::XMFLOAT4& color, int tile_index) {
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

std::vector<Vertex> BuildSelectionMesh(const Int3& block) {
  std::vector<Vertex> vertices;
  vertices.reserve(36u);
  const float expand = (kSelectionScale - 1.0f) * 0.5f * kBlockSize;
  const DirectX::XMFLOAT3 base{
      (block.x * kBlockSize) - expand,
      (block.y * kBlockSize) - expand,
      (block.z * kBlockSize) - expand,
  };
  const DirectX::XMFLOAT4 highlight{1.0f, 1.0f, 0.2f, 1.0f};
  for (const auto& face : kFaces) {
    const DirectX::XMFLOAT4 shaded = ApplyShade(highlight, face.shade);
    AddFaceScaled(vertices, base, kSelectionScale, face, shaded, kTileGrassTop);
  }
  return vertices;
}

bool UploadChunkMesh(Chunk& chunk, const std::vector<Vertex>& vertices) {
  if (!g_d3d.device || !g_d3d.context) {
    return false;
  }
  if (vertices.empty()) {
    chunk.vertex_count = 0;
    return true;
  }
  chunk.vertex_count = static_cast<UINT>(vertices.size());
  const UINT byte_size = chunk.vertex_count * sizeof(Vertex);
  if (!chunk.vertex_buffer || byte_size > chunk.vertex_buffer_size) {
    chunk.vertex_buffer.Reset();
    D3D11_BUFFER_DESC buffer_desc{};
    buffer_desc.ByteWidth = byte_size;
    buffer_desc.Usage = D3D11_USAGE_DYNAMIC;
    buffer_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    HRESULT hr = g_d3d.device->CreateBuffer(&buffer_desc, nullptr,
                                            &chunk.vertex_buffer);
    if (FAILED(hr)) {
      ShowError("Failed to create voxel vertex buffer", hr);
      return false;
    }
    chunk.vertex_buffer_size = byte_size;
  }

  D3D11_MAPPED_SUBRESOURCE mapped{};
  HRESULT hr = g_d3d.context->Map(chunk.vertex_buffer.Get(), 0,
                                  D3D11_MAP_WRITE_DISCARD, 0, &mapped);
  if (FAILED(hr)) {
    ShowError("Failed to map voxel vertex buffer", hr);
    return false;
  }
  std::memcpy(mapped.pData, vertices.data(), byte_size);
  g_d3d.context->Unmap(chunk.vertex_buffer.Get(), 0);
  return true;
}

bool UploadSelectionMesh(const std::vector<Vertex>& vertices) {
  if (!g_d3d.device || !g_d3d.context) {
    return false;
  }
  if (vertices.empty()) {
    g_d3d.highlight_vertex_count = 0;
    return true;
  }
  g_d3d.highlight_vertex_count = static_cast<UINT>(vertices.size());
  const UINT byte_size = g_d3d.highlight_vertex_count * sizeof(Vertex);
  if (!g_d3d.highlight_vertex_buffer ||
      byte_size > g_d3d.highlight_vertex_buffer_size) {
    g_d3d.highlight_vertex_buffer.Reset();
    D3D11_BUFFER_DESC buffer_desc{};
    buffer_desc.ByteWidth = byte_size;
    buffer_desc.Usage = D3D11_USAGE_DYNAMIC;
    buffer_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    HRESULT hr = g_d3d.device->CreateBuffer(&buffer_desc, nullptr,
                                            &g_d3d.highlight_vertex_buffer);
    if (FAILED(hr)) {
      ShowError("Failed to create selection vertex buffer", hr);
      return false;
    }
    g_d3d.highlight_vertex_buffer_size = byte_size;
  }

  D3D11_MAPPED_SUBRESOURCE mapped{};
  HRESULT hr = g_d3d.context->Map(g_d3d.highlight_vertex_buffer.Get(), 0,
                                  D3D11_MAP_WRITE_DISCARD, 0, &mapped);
  if (FAILED(hr)) {
    ShowError("Failed to map selection vertex buffer", hr);
    return false;
  }
  std::memcpy(mapped.pData, vertices.data(), byte_size);
  g_d3d.context->Unmap(g_d3d.highlight_vertex_buffer.Get(), 0);
  return true;
}

std::vector<Vertex> BuildHudMesh() {
  std::vector<Vertex> vertices;
  if (g_d3d.width == 0 || g_d3d.height == 0) {
    return vertices;
  }

  const float screen_w = static_cast<float>(g_d3d.width);
  const float screen_h = static_cast<float>(g_d3d.height);
  const DirectX::XMFLOAT4 white{1.0f, 1.0f, 1.0f, 1.0f};

  const float cx = screen_w * 0.5f;
  const float cy = screen_h * 0.5f;
  const float t = kCrosshairThickness;
  const float len = kCrosshairLength;
  const float gap = kCrosshairGap;
  AddQuadPixels(vertices, cx - gap - len, cy - t * 0.5f, len, t, white, screen_w,
                screen_h);
  AddQuadPixels(vertices, cx + gap, cy - t * 0.5f, len, t, white, screen_w,
                screen_h);
  AddQuadPixels(vertices, cx - t * 0.5f, cy - gap - len, t, len, white, screen_w,
                screen_h);
  AddQuadPixels(vertices, cx - t * 0.5f, cy + gap, t, len, white, screen_w,
                screen_h);

  char buffer[64];
  float x = kHudPadding;
  float y = kHudPadding;
  const float line_height = (7.0f + 3.0f) * kHudScale;

  std::snprintf(buffer, sizeof(buffer), "FPS:%d", static_cast<int>(g_fps + 0.5f));
  DrawText(vertices, x, y, kHudScale, buffer, white, screen_w, screen_h);
  y += line_height;

  const int pos_x = static_cast<int>(std::floor(g_camera.position.x));
  const int pos_y = static_cast<int>(std::floor(g_camera.position.y));
  const int pos_z = static_cast<int>(std::floor(g_camera.position.z));
  std::snprintf(buffer, sizeof(buffer), "X:%d Y:%d Z:%d", pos_x, pos_y, pos_z);
  DrawText(vertices, x, y, kHudScale, buffer, white, screen_w, screen_h);
  y += line_height;

  int block_id = -1;
  if (g_hover_valid) {
    block_id = static_cast<int>(GetBlock(g_world, g_hover_hit.block.x,
                                         g_hover_hit.block.y, g_hover_hit.block.z));
  }
  std::snprintf(buffer, sizeof(buffer), "B:%d", block_id);
  DrawText(vertices, x, y, kHudScale, buffer, white, screen_w, screen_h);

  return vertices;
}

bool UploadHudMesh(const std::vector<Vertex>& vertices) {
  if (!g_d3d.device || !g_d3d.context) {
    return false;
  }
  if (vertices.empty()) {
    g_d3d.hud_vertex_count = 0;
    return true;
  }
  g_d3d.hud_vertex_count = static_cast<UINT>(vertices.size());
  const UINT byte_size = g_d3d.hud_vertex_count * sizeof(Vertex);
  if (!g_d3d.hud_vertex_buffer || byte_size > g_d3d.hud_vertex_buffer_size) {
    g_d3d.hud_vertex_buffer.Reset();
    D3D11_BUFFER_DESC buffer_desc{};
    buffer_desc.ByteWidth = byte_size;
    buffer_desc.Usage = D3D11_USAGE_DYNAMIC;
    buffer_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    HRESULT hr = g_d3d.device->CreateBuffer(&buffer_desc, nullptr,
                                            &g_d3d.hud_vertex_buffer);
    if (FAILED(hr)) {
      ShowError("Failed to create HUD vertex buffer", hr);
      return false;
    }
    g_d3d.hud_vertex_buffer_size = byte_size;
  }

  D3D11_MAPPED_SUBRESOURCE mapped{};
  HRESULT hr = g_d3d.context->Map(g_d3d.hud_vertex_buffer.Get(), 0,
                                  D3D11_MAP_WRITE_DISCARD, 0, &mapped);
  if (FAILED(hr)) {
    ShowError("Failed to map HUD vertex buffer", hr);
    return false;
  }
  std::memcpy(mapped.pData, vertices.data(), byte_size);
  g_d3d.context->Unmap(g_d3d.hud_vertex_buffer.Get(), 0);
  return true;
}

bool NextToken(std::istream& input, std::string& token) {
  token.clear();
  char ch = '\0';
  while (input.get(ch)) {
    if (std::isspace(static_cast<unsigned char>(ch))) {
      continue;
    }
    if (ch == '#') {
      std::string line;
      std::getline(input, line);
      continue;
    }
    token.push_back(ch);
    while (input.get(ch)) {
      if (std::isspace(static_cast<unsigned char>(ch))) {
        break;
      }
      if (ch == '#') {
        std::string line;
        std::getline(input, line);
        break;
      }
      token.push_back(ch);
    }
    return true;
  }
  return false;
}

bool LoadPPMFile(const char* path, std::vector<uint8_t>& pixels, int& width,
                 int& height) {
  std::ifstream file(path);
  if (!file.is_open()) {
    return false;
  }

  std::string token;
  if (!NextToken(file, token) || token != "P3") {
    return false;
  }
  if (!NextToken(file, token)) {
    return false;
  }
  width = static_cast<int>(std::strtol(token.c_str(), nullptr, 10));
  if (!NextToken(file, token)) {
    return false;
  }
  height = static_cast<int>(std::strtol(token.c_str(), nullptr, 10));
  if (!NextToken(file, token)) {
    return false;
  }
  const int max_value = static_cast<int>(std::strtol(token.c_str(), nullptr, 10));
  if (width <= 0 || height <= 0 || max_value <= 0) {
    return false;
  }

  const int pixel_count = width * height;
  pixels.resize(static_cast<size_t>(pixel_count) * 4u);
  for (int i = 0; i < pixel_count; ++i) {
    if (!NextToken(file, token)) {
      return false;
    }
    int r = static_cast<int>(std::strtol(token.c_str(), nullptr, 10));
    if (!NextToken(file, token)) {
      return false;
    }
    int g = static_cast<int>(std::strtol(token.c_str(), nullptr, 10));
    if (!NextToken(file, token)) {
      return false;
    }
    int b = static_cast<int>(std::strtol(token.c_str(), nullptr, 10));

    if (max_value != 255 && max_value > 0) {
      r = (r * 255) / max_value;
      g = (g * 255) / max_value;
      b = (b * 255) / max_value;
    }

    r = std::clamp(r, 0, 255);
    g = std::clamp(g, 0, 255);
    b = std::clamp(b, 0, 255);

    const size_t base = static_cast<size_t>(i) * 4u;
    pixels[base + 0] = static_cast<uint8_t>(r);
    pixels[base + 1] = static_cast<uint8_t>(g);
    pixels[base + 2] = static_cast<uint8_t>(b);
    pixels[base + 3] = 255u;
  }

  return true;
}

bool CreateTextureAtlas() {
  if (!g_d3d.device) {
    return false;
  }
  const char* paths[] = {
      "assets/atlas.ppm",
      "dx11/assets/atlas.ppm",
      "../assets/atlas.ppm",
      "../../dx11/assets/atlas.ppm",
  };

  std::vector<uint8_t> pixels;
  int width = 0;
  int height = 0;
  bool loaded = false;
  for (const char* path : paths) {
    if (LoadPPMFile(path, pixels, width, height)) {
      loaded = true;
      break;
    }
  }

  if (!loaded) {
    MessageBoxA(nullptr, "Failed to load assets/atlas.ppm.",
                "Texture Load Error", MB_ICONERROR);
    return false;
  }

  if ((width % kAtlasTilesX) != 0 || (height % kAtlasTilesY) != 0) {
    MessageBoxA(nullptr, "Atlas size does not match tile layout.",
                "Texture Warning", MB_ICONWARNING);
  }

  D3D11_TEXTURE2D_DESC desc{};
  desc.Width = static_cast<UINT>(width);
  desc.Height = static_cast<UINT>(height);
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_IMMUTABLE;
  desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

  D3D11_SUBRESOURCE_DATA init_data{};
  init_data.pSysMem = pixels.data();
  init_data.SysMemPitch = static_cast<UINT>(width * 4);

  Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
  HRESULT hr = g_d3d.device->CreateTexture2D(&desc, &init_data, &texture);
  if (FAILED(hr)) {
    ShowError("Failed to create texture atlas", hr);
    return false;
  }

  D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc{};
  srv_desc.Format = desc.Format;
  srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
  srv_desc.Texture2D.MipLevels = 1;

  hr = g_d3d.device->CreateShaderResourceView(texture.Get(), &srv_desc,
                                              &g_d3d.texture_srv);
  if (FAILED(hr)) {
    ShowError("Failed to create texture SRV", hr);
    return false;
  }

  D3D11_SAMPLER_DESC sampler_desc{};
  sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
  sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
  sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
  sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
  sampler_desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
  sampler_desc.MinLOD = 0.0f;
  sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;

  hr = g_d3d.device->CreateSamplerState(&sampler_desc, &g_d3d.sampler_state);
  if (FAILED(hr)) {
    ShowError("Failed to create sampler state", hr);
    return false;
  }

  return true;
}

bool CompileShader(const char* source, const char* entry, const char* target,
                   Microsoft::WRL::ComPtr<ID3DBlob>& shader_blob) {
  if (!LoadD3DCompiler()) {
    return false;
  }
  UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
  flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
  Microsoft::WRL::ComPtr<ID3DBlob> error_blob;
  HRESULT hr = g_d3d_compile(source, std::strlen(source), nullptr, nullptr, nullptr,
                             entry, target, flags, 0, &shader_blob, &error_blob);
  if (FAILED(hr)) {
    if (error_blob) {
      MessageBoxA(nullptr,
                  static_cast<const char*>(error_blob->GetBufferPointer()),
                  "Shader Compile Error", MB_ICONERROR);
    } else {
      ShowError("Failed to compile shader", hr);
    }
    return false;
  }
  return true;
}

bool CreatePipeline() {
  const char* vs_source = R"(
    cbuffer Constants : register(b0) {
      float4x4 mvp;
    };
    struct VSInput {
      float3 position : POSITION;
      float4 color : COLOR;
      float2 uv : TEXCOORD0;
    };
    struct VSOutput {
      float4 position : SV_POSITION;
      float4 color : COLOR;
      float2 uv : TEXCOORD0;
    };
    VSOutput main(VSInput input) {
      VSOutput output;
      output.position = mul(float4(input.position, 1.0f), mvp);
      output.color = input.color;
      output.uv = input.uv;
      return output;
    }
  )";

  const char* ps_source = R"(
    Texture2D atlas : register(t0);
    SamplerState atlasSampler : register(s0);
    struct PSInput {
      float4 position : SV_POSITION;
      float4 color : COLOR;
      float2 uv : TEXCOORD0;
    };
    float4 main(PSInput input) : SV_TARGET {
      return atlas.Sample(atlasSampler, input.uv) * input.color;
    }
  )";

  Microsoft::WRL::ComPtr<ID3DBlob> vs_blob;
  if (!CompileShader(vs_source, "main", "vs_5_0", vs_blob)) {
    return false;
  }
  HRESULT hr = g_d3d.device->CreateVertexShader(
      vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), nullptr,
      &g_d3d.vertex_shader);
  if (FAILED(hr)) {
    ShowError("Failed to create vertex shader", hr);
    return false;
  }

  Microsoft::WRL::ComPtr<ID3DBlob> ps_blob;
  if (!CompileShader(ps_source, "main", "ps_5_0", ps_blob)) {
    return false;
  }
  hr = g_d3d.device->CreatePixelShader(
      ps_blob->GetBufferPointer(), ps_blob->GetBufferSize(), nullptr,
      &g_d3d.pixel_shader);
  if (FAILED(hr)) {
    ShowError("Failed to create pixel shader", hr);
    return false;
  }

  const char* solid_ps_source = R"(
    struct PSInput {
      float4 position : SV_POSITION;
      float4 color : COLOR;
      float2 uv : TEXCOORD0;
    };
    float4 main(PSInput input) : SV_TARGET {
      return input.color;
    }
  )";

  Microsoft::WRL::ComPtr<ID3DBlob> solid_ps_blob;
  if (!CompileShader(solid_ps_source, "main", "ps_5_0", solid_ps_blob)) {
    return false;
  }
  hr = g_d3d.device->CreatePixelShader(
      solid_ps_blob->GetBufferPointer(), solid_ps_blob->GetBufferSize(), nullptr,
      &g_d3d.solid_pixel_shader);
  if (FAILED(hr)) {
    ShowError("Failed to create solid pixel shader", hr);
    return false;
  }

  const D3D11_INPUT_ELEMENT_DESC layout[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
       static_cast<UINT>(offsetof(Vertex, position)),
       D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
       static_cast<UINT>(offsetof(Vertex, color)),
       D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0,
       static_cast<UINT>(offsetof(Vertex, uv)), D3D11_INPUT_PER_VERTEX_DATA, 0},
  };
  hr = g_d3d.device->CreateInputLayout(
      layout, static_cast<UINT>(sizeof(layout) / sizeof(layout[0])),
      vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(),
      &g_d3d.input_layout);
  if (FAILED(hr)) {
    ShowError("Failed to create input layout", hr);
    return false;
  }

  D3D11_BUFFER_DESC constant_desc{};
  constant_desc.ByteWidth = sizeof(DirectX::XMFLOAT4X4);
  constant_desc.Usage = D3D11_USAGE_DEFAULT;
  constant_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  hr = g_d3d.device->CreateBuffer(&constant_desc, nullptr,
                                  &g_d3d.constant_buffer);
  if (FAILED(hr)) {
    ShowError("Failed to create constant buffer", hr);
    return false;
  }

  D3D11_RASTERIZER_DESC raster_desc{};
  raster_desc.FillMode = D3D11_FILL_SOLID;
  raster_desc.CullMode = D3D11_CULL_NONE;
  raster_desc.DepthClipEnable = TRUE;
  hr = g_d3d.device->CreateRasterizerState(&raster_desc,
                                           &g_d3d.rasterizer_state);
  if (FAILED(hr)) {
    ShowError("Failed to create rasterizer state", hr);
    return false;
  }

  D3D11_DEPTH_STENCIL_DESC depth_desc{};
  depth_desc.DepthEnable = TRUE;
  depth_desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
  depth_desc.DepthFunc = D3D11_COMPARISON_LESS;
  hr = g_d3d.device->CreateDepthStencilState(&depth_desc, &g_d3d.depth_state);
  if (FAILED(hr)) {
    ShowError("Failed to create depth stencil state", hr);
    return false;
  }

  depth_desc.DepthEnable = FALSE;
  depth_desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
  depth_desc.DepthFunc = D3D11_COMPARISON_ALWAYS;
  hr = g_d3d.device->CreateDepthStencilState(&depth_desc,
                                             &g_d3d.depth_state_no_depth);
  if (FAILED(hr)) {
    ShowError("Failed to create HUD depth stencil state", hr);
    return false;
  }

  D3D11_RASTERIZER_DESC wire_desc = raster_desc;
  wire_desc.FillMode = D3D11_FILL_WIREFRAME;
  wire_desc.CullMode = D3D11_CULL_NONE;
  hr = g_d3d.device->CreateRasterizerState(&wire_desc,
                                           &g_d3d.wireframe_state);
  if (FAILED(hr)) {
    ShowError("Failed to create wireframe rasterizer state", hr);
    return false;
  }

  if (!CreateTextureAtlas()) {
    return false;
  }

  g_d3d.vertex_stride = sizeof(Vertex);
  g_d3d.vertex_offset = 0;
  return true;
}

bool CreateRenderTarget() {
  Microsoft::WRL::ComPtr<ID3D11Texture2D> back_buffer;
  HRESULT hr = g_d3d.swap_chain->GetBuffer(0, IID_PPV_ARGS(&back_buffer));
  if (FAILED(hr)) {
    ShowError("Failed to get swap chain back buffer", hr);
    return false;
  }
  hr = g_d3d.device->CreateRenderTargetView(back_buffer.Get(), nullptr,
                                            &g_d3d.render_target);
  if (FAILED(hr)) {
    ShowError("Failed to create render target view", hr);
    return false;
  }

  D3D11_TEXTURE2D_DESC depth_desc{};
  depth_desc.Width = g_d3d.width;
  depth_desc.Height = g_d3d.height;
  depth_desc.MipLevels = 1;
  depth_desc.ArraySize = 1;
  depth_desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
  depth_desc.SampleDesc.Count = 1;
  depth_desc.Usage = D3D11_USAGE_DEFAULT;
  depth_desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
  hr = g_d3d.device->CreateTexture2D(&depth_desc, nullptr, &g_d3d.depth_buffer);
  if (FAILED(hr)) {
    ShowError("Failed to create depth buffer", hr);
    return false;
  }
  hr = g_d3d.device->CreateDepthStencilView(g_d3d.depth_buffer.Get(), nullptr,
                                            &g_d3d.depth_stencil_view);
  if (FAILED(hr)) {
    ShowError("Failed to create depth stencil view", hr);
    return false;
  }

  g_d3d.context->OMSetRenderTargets(1, g_d3d.render_target.GetAddressOf(),
                                    g_d3d.depth_stencil_view.Get());
  D3D11_VIEWPORT viewport{};
  viewport.Width = static_cast<float>(g_d3d.width);
  viewport.Height = static_cast<float>(g_d3d.height);
  viewport.MinDepth = 0.0f;
  viewport.MaxDepth = 1.0f;
  viewport.TopLeftX = 0.0f;
  viewport.TopLeftY = 0.0f;
  g_d3d.context->RSSetViewports(1, &viewport);
  return true;
}

bool CreateDeviceAndSwapChain(HWND hwnd, UINT width, UINT height) {
  g_d3d.hwnd = hwnd;
  g_d3d.width = width;
  g_d3d.height = height;

  DXGI_SWAP_CHAIN_DESC swap_desc{};
  swap_desc.BufferDesc.Width = width;
  swap_desc.BufferDesc.Height = height;
  swap_desc.BufferDesc.RefreshRate.Numerator = 60;
  swap_desc.BufferDesc.RefreshRate.Denominator = 1;
  swap_desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  swap_desc.SampleDesc.Count = 1;
  swap_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  swap_desc.BufferCount = 1;
  swap_desc.OutputWindow = hwnd;
  swap_desc.Windowed = TRUE;
  swap_desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

  D3D_FEATURE_LEVEL feature_level = D3D_FEATURE_LEVEL_11_0;
  HRESULT hr = D3D11CreateDeviceAndSwapChain(
      nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
      D3D11_SDK_VERSION, &swap_desc, &g_d3d.swap_chain, &g_d3d.device,
      &feature_level, &g_d3d.context);
  if (FAILED(hr)) {
    ShowError("Failed to create D3D11 device and swap chain", hr);
    return false;
  }

  if (!CreateRenderTarget()) {
    return false;
  }
  if (!CreatePipeline()) {
    return false;
  }

  StreamChunks();
  if (!UpdateChunkMeshes()) {
    return false;
  }

  return true;
}

void Resize(UINT width, UINT height) {
  if (!g_d3d.swap_chain || width == 0 || height == 0) {
    return;
  }
  g_d3d.width = width;
  g_d3d.height = height;
  g_d3d.context->OMSetRenderTargets(0, nullptr, nullptr);
  g_d3d.render_target.Reset();
  g_d3d.depth_stencil_view.Reset();
  g_d3d.depth_buffer.Reset();
  HRESULT hr = g_d3d.swap_chain->ResizeBuffers(0, width, height,
                                               DXGI_FORMAT_UNKNOWN, 0);
  if (FAILED(hr)) {
    ShowError("Failed to resize swap chain buffers", hr);
    return;
  }
  CreateRenderTarget();
  if (g_mouse_captured) {
    UpdateClipRect();
    CenterCursor();
  }
}

void Render() {
  if (!g_d3d.context || !g_d3d.render_target || !g_d3d.swap_chain) {
    return;
  }
  const float clear_color[4] = {0.18f, 0.28f, 0.45f, 1.0f};
  g_d3d.context->OMSetRenderTargets(1, g_d3d.render_target.GetAddressOf(),
                                    g_d3d.depth_stencil_view.Get());
  if (g_d3d.depth_state) {
    g_d3d.context->OMSetDepthStencilState(g_d3d.depth_state.Get(), 0);
  }
  g_d3d.context->ClearRenderTargetView(g_d3d.render_target.Get(), clear_color);
  if (g_d3d.depth_stencil_view) {
    g_d3d.context->ClearDepthStencilView(g_d3d.depth_stencil_view.Get(),
                                         D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL,
                                         1.0f, 0);
  }
  if (g_d3d.vertex_shader && g_d3d.pixel_shader && g_d3d.input_layout &&
      g_d3d.constant_buffer && g_d3d.texture_srv && g_d3d.sampler_state) {
    const float aspect =
        (g_d3d.height == 0) ? 1.0f
                            : static_cast<float>(g_d3d.width) /
                                  static_cast<float>(g_d3d.height);
    const DirectX::XMMATRIX world = DirectX::XMMatrixIdentity();
    const DirectX::XMVECTOR eye = DirectX::XMLoadFloat3(&g_camera.position);
    const DirectX::XMVECTOR forward = GetCameraForward();
    const DirectX::XMVECTOR up = DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    const DirectX::XMMATRIX view = DirectX::XMMatrixLookToLH(eye, forward, up);
    const DirectX::XMMATRIX proj =
        DirectX::XMMatrixPerspectiveFovLH(DirectX::XMConvertToRadians(60.0f),
                                          aspect, 0.1f, 200.0f);
    const DirectX::XMMATRIX view_proj = view * proj;
    const DirectX::XMMATRIX mvp =
        DirectX::XMMatrixTranspose(world * view * proj);
    DirectX::XMFLOAT4X4 mvp_matrix;
    DirectX::XMStoreFloat4x4(&mvp_matrix, mvp);
    g_d3d.context->UpdateSubresource(g_d3d.constant_buffer.Get(), 0, nullptr,
                                     &mvp_matrix, 0, 0);

    g_d3d.context->IASetInputLayout(g_d3d.input_layout.Get());
    g_d3d.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_d3d.context->VSSetShader(g_d3d.vertex_shader.Get(), nullptr, 0);
    g_d3d.context->VSSetConstantBuffers(0, 1, g_d3d.constant_buffer.GetAddressOf());
    g_d3d.context->PSSetShader(g_d3d.pixel_shader.Get(), nullptr, 0);
    g_d3d.context->PSSetShaderResources(0, 1, g_d3d.texture_srv.GetAddressOf());
    g_d3d.context->PSSetSamplers(0, 1, g_d3d.sampler_state.GetAddressOf());
    g_d3d.context->RSSetState(g_d3d.rasterizer_state.Get());
    for (const auto& entry : g_world.chunks) {
      const Chunk& chunk = entry.second;
      if (!chunk.vertex_buffer || chunk.vertex_count == 0) {
        continue;
      }
      if (!IsChunkVisible(view_proj, chunk)) {
        continue;
      }
      ID3D11Buffer* buffer = chunk.vertex_buffer.Get();
      g_d3d.context->IASetVertexBuffers(0, 1, &buffer, &g_d3d.vertex_stride,
                                        &g_d3d.vertex_offset);
      g_d3d.context->Draw(chunk.vertex_count, 0);
    }
  }
  if (g_d3d.wireframe_state && g_d3d.solid_pixel_shader &&
      g_d3d.highlight_vertex_buffer && g_d3d.highlight_vertex_count > 0) {
    g_d3d.context->IASetInputLayout(g_d3d.input_layout.Get());
    g_d3d.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_d3d.context->IASetVertexBuffers(
        0, 1, g_d3d.highlight_vertex_buffer.GetAddressOf(), &g_d3d.vertex_stride,
        &g_d3d.vertex_offset);
    g_d3d.context->VSSetShader(g_d3d.vertex_shader.Get(), nullptr, 0);
    g_d3d.context->VSSetConstantBuffers(0, 1,
                                        g_d3d.constant_buffer.GetAddressOf());
    g_d3d.context->PSSetShader(g_d3d.solid_pixel_shader.Get(), nullptr, 0);
    g_d3d.context->RSSetState(g_d3d.wireframe_state.Get());
    g_d3d.context->Draw(g_d3d.highlight_vertex_count, 0);
  }
  if (g_d3d.hud_vertex_buffer && g_d3d.hud_vertex_count > 0 &&
      g_d3d.solid_pixel_shader && g_d3d.depth_state_no_depth) {
    const DirectX::XMMATRIX identity = DirectX::XMMatrixIdentity();
    DirectX::XMFLOAT4X4 mvp_matrix;
    DirectX::XMStoreFloat4x4(&mvp_matrix, identity);
    g_d3d.context->UpdateSubresource(g_d3d.constant_buffer.Get(), 0, nullptr,
                                     &mvp_matrix, 0, 0);
    g_d3d.context->OMSetDepthStencilState(g_d3d.depth_state_no_depth.Get(), 0);
    g_d3d.context->IASetInputLayout(g_d3d.input_layout.Get());
    g_d3d.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_d3d.context->IASetVertexBuffers(
        0, 1, g_d3d.hud_vertex_buffer.GetAddressOf(), &g_d3d.vertex_stride,
        &g_d3d.vertex_offset);
    g_d3d.context->VSSetShader(g_d3d.vertex_shader.Get(), nullptr, 0);
    g_d3d.context->VSSetConstantBuffers(0, 1,
                                        g_d3d.constant_buffer.GetAddressOf());
    g_d3d.context->PSSetShader(g_d3d.solid_pixel_shader.Get(), nullptr, 0);
    g_d3d.context->RSSetState(g_d3d.rasterizer_state.Get());
    g_d3d.context->Draw(g_d3d.hud_vertex_count, 0);
  }
  g_d3d.swap_chain->Present(1, 0);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  switch (message) {
    case WM_ACTIVATE:
      if (LOWORD(wparam) == WA_INACTIVE) {
        SetMouseCaptured(false);
      }
      return 0;
    case WM_LBUTTONDOWN:
      if (!g_mouse_captured) {
        SetMouseCaptured(true);
      }
      return 0;
    case WM_SIZE: {
      if (wparam != SIZE_MINIMIZED) {
        const UINT width = LOWORD(lparam);
        const UINT height = HIWORD(lparam);
        Resize(width, height);
      }
      return 0;
    }
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
    default:
      return DefWindowProc(hwnd, message, wparam, lparam);
  }
}
}  // namespace

int WINAPI wWinMain(_In_ HINSTANCE instance, _In_opt_ HINSTANCE, _In_ PWSTR,
                    _In_ int show_cmd) {
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = WindowProc;
  wc.hInstance = instance;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.lpszClassName = kWindowClassName;

  if (!RegisterClassExW(&wc)) {
    MessageBoxA(nullptr, "Failed to register window class", "Error", MB_ICONERROR);
    return 0;
  }

  RECT rect{0, 0, kInitialWidth, kInitialHeight};
  AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
  HWND hwnd = CreateWindowExW(0, kWindowClassName, kWindowTitle,
                              WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                              rect.right - rect.left, rect.bottom - rect.top,
                              nullptr, nullptr, instance, nullptr);
  if (!hwnd) {
    MessageBoxA(nullptr, "Failed to create window", "Error", MB_ICONERROR);
    return 0;
  }

  ShowWindow(hwnd, show_cmd);
  UpdateWindow(hwnd);

  RECT client_rect{};
  GetClientRect(hwnd, &client_rect);
  const UINT width = static_cast<UINT>(client_rect.right - client_rect.left);
  const UINT height = static_cast<UINT>(client_rect.bottom - client_rect.top);
  if (!CreateDeviceAndSwapChain(hwnd, width, height)) {
    return 0;
  }

  SetMouseCaptured(true);

  LARGE_INTEGER frequency{};
  QueryPerformanceFrequency(&frequency);
  LARGE_INTEGER last_time{};
  QueryPerformanceCounter(&last_time);

  MSG msg{};
  while (msg.message != WM_QUIT) {
    if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    } else {
      LARGE_INTEGER now{};
      QueryPerformanceCounter(&now);
      float dt = static_cast<float>(now.QuadPart - last_time.QuadPart) /
                 static_cast<float>(frequency.QuadPart);
      last_time = now;
      if (dt > 0.1f) {
        dt = 0.1f;
      }
      UpdateFps(dt);
      UpdateInput(dt);
      StreamChunks();
      UpdateChunkMeshes();
      UpdateHoverHit();
      const bool changed = HandleBlockInteraction();
      if (changed) {
        UpdateChunkMeshes();
        UpdateHoverHit();
      }
      UpdateSelectionMesh();
      UploadHudMesh(BuildHudMesh());
      Render();
    }
  }

  SetMouseCaptured(false);

  if (g_d3d.context) {
    g_d3d.context->ClearState();
  }
  if (g_d3dcompiler_module) {
    FreeLibrary(g_d3dcompiler_module);
    g_d3dcompiler_module = nullptr;
    g_d3d_compile = nullptr;
  }

  return 0;
}
