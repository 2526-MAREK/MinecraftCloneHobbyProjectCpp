#include <windows.h>

#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <DirectXMath.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
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
constexpr float kMoveSpeed = 6.0f;
constexpr float kMouseSensitivity = 0.002f;
constexpr float kMaxPitch = DirectX::XM_PIDIV2 - 0.01f;
constexpr float kRaycastDistance = 8.0f;

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
  Microsoft::WRL::ComPtr<ID3D11Buffer> vertex_buffer;
  Microsoft::WRL::ComPtr<ID3D11Buffer> constant_buffer;
  Microsoft::WRL::ComPtr<ID3D11RasterizerState> rasterizer_state;
  UINT vertex_stride = 0;
  UINT vertex_offset = 0;
  UINT vertex_count = 0;
  UINT vertex_buffer_size = 0;
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

std::vector<Vertex> BuildVoxelMesh(const VoxelChunk& chunk);
bool UploadVoxelMesh(const std::vector<Vertex>& vertices);

D3DState g_d3d;
VoxelChunk g_chunk;
CameraState g_camera = {{0.0f, 6.0f, -14.0f}, 0.0f, 0.0f, kMoveSpeed,
                         kMouseSensitivity};
bool g_mouse_captured = false;
bool g_escape_down = false;
bool g_lmb_down = false;
bool g_rmb_down = false;

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

bool InBounds(int x, int y, int z) {
  return x >= 0 && x < kChunkSize && y >= 0 && y < kChunkSize && z >= 0 &&
         z < kChunkSize;
}

bool SetBlockInChunk(VoxelChunk& chunk, int x, int y, int z, BlockId id) {
  if (!InBounds(x, y, z)) {
    return false;
  }
  if (chunk.Get(x, y, z) == id) {
    return false;
  }
  chunk.Set(x, y, z, id);
  return true;
}

struct RayHit {
  bool hit = false;
  Int3 block{0, 0, 0};
  Int3 previous{0, 0, 0};
};

RayHit RaycastVoxel(const VoxelChunk& chunk, const DirectX::XMFLOAT3& origin,
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

  const float half = 0.5f * kChunkSize * kBlockSize;
  const float ox = origin.x + half;
  const float oy = origin.y;
  const float oz = origin.z + half;

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

  if (InBounds(current.x, current.y, current.z) &&
      chunk.Get(current.x, current.y, current.z) != BlockId::Air) {
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

    if (InBounds(current.x, current.y, current.z) &&
        chunk.Get(current.x, current.y, current.z) != BlockId::Air) {
      result.hit = true;
      result.block = current;
      result.previous = previous;
      return result;
    }
  }

  return result;
}

bool HandleBlockInteraction() {
  const bool lmb = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
  const bool rmb = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
  const bool lmb_pressed = lmb && !g_lmb_down;
  const bool rmb_pressed = rmb && !g_rmb_down;
  g_lmb_down = lmb;
  g_rmb_down = rmb;

  if (!g_mouse_captured || (!lmb_pressed && !rmb_pressed)) {
    return false;
  }

  const DirectX::XMVECTOR forward_vec = GetCameraForward();
  DirectX::XMFLOAT3 forward{};
  DirectX::XMStoreFloat3(&forward, forward_vec);
  const DirectX::XMFLOAT3 origin = g_camera.position;
  const RayHit hit = RaycastVoxel(g_chunk, origin, forward, kRaycastDistance);
  if (!hit.hit) {
    return false;
  }

  bool changed = false;
  if (lmb_pressed) {
    changed = SetBlockInChunk(g_chunk, hit.block.x, hit.block.y, hit.block.z,
                              BlockId::Air);
  }
  if (rmb_pressed) {
    if (InBounds(hit.previous.x, hit.previous.y, hit.previous.z) &&
        g_chunk.Get(hit.previous.x, hit.previous.y, hit.previous.z) ==
            BlockId::Air) {
      changed = SetBlockInChunk(g_chunk, hit.previous.x, hit.previous.y,
                                hit.previous.z, BlockId::Dirt) ||
                changed;
    }
  }

  if (changed) {
    const std::vector<Vertex> vertices = BuildVoxelMesh(g_chunk);
    UploadVoxelMesh(vertices);
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

BlockId GetBlockSafe(const VoxelChunk& chunk, int x, int y, int z) {
  if (x < 0 || x >= kChunkSize || y < 0 || y >= kChunkSize || z < 0 ||
      z >= kChunkSize) {
    return BlockId::Air;
  }
  return chunk.Get(x, y, z);
}

DirectX::XMFLOAT4 BlockFaceColor(BlockId id, FaceDir dir) {
  switch (id) {
    case BlockId::Grass:
      if (dir == FaceDir::PosY) {
        return {0.35f, 0.70f, 0.35f, 1.0f};
      }
      if (dir == FaceDir::NegY) {
        return {0.43f, 0.33f, 0.22f, 1.0f};
      }
      return {0.30f, 0.62f, 0.30f, 1.0f};
    case BlockId::Dirt:
      return {0.47f, 0.35f, 0.24f, 1.0f};
    case BlockId::Stone:
      return {0.55f, 0.55f, 0.55f, 1.0f};
    case BlockId::Air:
      break;
  }
  return {0.0f, 0.0f, 0.0f, 0.0f};
}

DirectX::XMFLOAT4 ApplyShade(const DirectX::XMFLOAT4& color, float shade) {
  return {color.x * shade, color.y * shade, color.z * shade, color.w};
}

void AddFace(std::vector<Vertex>& vertices, const DirectX::XMFLOAT3& base,
             const FaceDef& face, const DirectX::XMFLOAT4& color) {
  constexpr int indices[6] = {0, 1, 2, 0, 2, 3};
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
    vertices.push_back(vertex);
  }
}

std::vector<Vertex> BuildVoxelMesh(const VoxelChunk& chunk) {
  std::vector<Vertex> vertices;
  vertices.reserve(static_cast<size_t>(kChunkVolume) * 36u);
  const float half = 0.5f * kChunkSize * kBlockSize;
  for (int z = 0; z < kChunkSize; ++z) {
    for (int y = 0; y < kChunkSize; ++y) {
      for (int x = 0; x < kChunkSize; ++x) {
        const BlockId id = chunk.Get(x, y, z);
        if (id == BlockId::Air) {
          continue;
        }
        for (const auto& face : kFaces) {
          const BlockId neighbor =
              GetBlockSafe(chunk, x + face.neighbor.x, y + face.neighbor.y,
                           z + face.neighbor.z);
          if (neighbor != BlockId::Air) {
            continue;
          }
          const DirectX::XMFLOAT3 base{
              (x * kBlockSize) - half,
              y * kBlockSize,
              (z * kBlockSize) - half,
          };
          const DirectX::XMFLOAT4 base_color = BlockFaceColor(id, face.dir);
          const DirectX::XMFLOAT4 shaded = ApplyShade(base_color, face.shade);
          AddFace(vertices, base, face, shaded);
        }
      }
    }
  }
  return vertices;
}

bool UploadVoxelMesh(const std::vector<Vertex>& vertices) {
  if (!g_d3d.device || !g_d3d.context) {
    return false;
  }
  if (vertices.empty()) {
    g_d3d.vertex_count = 0;
    return true;
  }
  g_d3d.vertex_count = static_cast<UINT>(vertices.size());
  const UINT byte_size = g_d3d.vertex_count * sizeof(Vertex);
  if (!g_d3d.vertex_buffer || byte_size > g_d3d.vertex_buffer_size) {
    g_d3d.vertex_buffer.Reset();
    D3D11_BUFFER_DESC buffer_desc{};
    buffer_desc.ByteWidth = byte_size;
    buffer_desc.Usage = D3D11_USAGE_DYNAMIC;
    buffer_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    HRESULT hr = g_d3d.device->CreateBuffer(&buffer_desc, nullptr,
                                            &g_d3d.vertex_buffer);
    if (FAILED(hr)) {
      ShowError("Failed to create voxel vertex buffer", hr);
      return false;
    }
    g_d3d.vertex_buffer_size = byte_size;
  }

  D3D11_MAPPED_SUBRESOURCE mapped{};
  HRESULT hr = g_d3d.context->Map(g_d3d.vertex_buffer.Get(), 0,
                                  D3D11_MAP_WRITE_DISCARD, 0, &mapped);
  if (FAILED(hr)) {
    ShowError("Failed to map voxel vertex buffer", hr);
    return false;
  }
  std::memcpy(mapped.pData, vertices.data(), byte_size);
  g_d3d.context->Unmap(g_d3d.vertex_buffer.Get(), 0);
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
    };
    struct VSOutput {
      float4 position : SV_POSITION;
      float4 color : COLOR;
    };
    VSOutput main(VSInput input) {
      VSOutput output;
      output.position = mul(float4(input.position, 1.0f), mvp);
      output.color = input.color;
      return output;
    }
  )";

  const char* ps_source = R"(
    struct PSInput {
      float4 position : SV_POSITION;
      float4 color : COLOR;
    };
    float4 main(PSInput input) : SV_TARGET {
      return input.color;
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

  const D3D11_INPUT_ELEMENT_DESC layout[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
       D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12,
       D3D11_INPUT_PER_VERTEX_DATA, 0},
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

  GenerateFlatChunk(g_chunk);
  const std::vector<Vertex> vertices = BuildVoxelMesh(g_chunk);
  if (!UploadVoxelMesh(vertices)) {
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
  g_d3d.context->ClearRenderTargetView(g_d3d.render_target.Get(), clear_color);
  if (g_d3d.depth_stencil_view) {
    g_d3d.context->ClearDepthStencilView(g_d3d.depth_stencil_view.Get(),
                                         D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL,
                                         1.0f, 0);
  }
  if (g_d3d.vertex_shader && g_d3d.pixel_shader && g_d3d.input_layout &&
      g_d3d.vertex_buffer && g_d3d.constant_buffer && g_d3d.vertex_count > 0) {
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
    const DirectX::XMMATRIX mvp = DirectX::XMMatrixTranspose(world * view * proj);
    DirectX::XMFLOAT4X4 mvp_matrix;
    DirectX::XMStoreFloat4x4(&mvp_matrix, mvp);
    g_d3d.context->UpdateSubresource(g_d3d.constant_buffer.Get(), 0, nullptr,
                                     &mvp_matrix, 0, 0);

    g_d3d.context->IASetInputLayout(g_d3d.input_layout.Get());
    g_d3d.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_d3d.context->IASetVertexBuffers(
        0, 1, g_d3d.vertex_buffer.GetAddressOf(), &g_d3d.vertex_stride,
        &g_d3d.vertex_offset);
    g_d3d.context->VSSetShader(g_d3d.vertex_shader.Get(), nullptr, 0);
    g_d3d.context->VSSetConstantBuffers(0, 1, g_d3d.constant_buffer.GetAddressOf());
    g_d3d.context->PSSetShader(g_d3d.pixel_shader.Get(), nullptr, 0);
    g_d3d.context->RSSetState(g_d3d.rasterizer_state.Get());
    g_d3d.context->Draw(g_d3d.vertex_count, 0);
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
      UpdateInput(dt);
      HandleBlockInteraction();
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
