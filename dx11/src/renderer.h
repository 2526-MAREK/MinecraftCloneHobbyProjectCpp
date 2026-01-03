#pragma once

#include <windows.h>

#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <DirectXMath.h>

#include <unordered_map>
#include <vector>

#include "camera.h"
#include "world.h"

constexpr float kSelectionScale = 1.03f;
constexpr float kHudScale = 2.0f;
constexpr float kHudPadding = 12.0f;
constexpr float kCrosshairLength = 10.0f;
constexpr float kCrosshairGap = 6.0f;
constexpr float kCrosshairThickness = 2.0f;

struct ChunkMesh {
  Microsoft::WRL::ComPtr<ID3D11Buffer> vertex_buffer;
  UINT vertex_count = 0;
  UINT vertex_buffer_size = 0;
};

struct RendererState {
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
  std::unordered_map<Int3, ChunkMesh, Int3Hash> chunk_meshes;
};

bool InitRenderer(RendererState& renderer, HWND hwnd, UINT width, UINT height);
void ShutdownRenderer(RendererState& renderer);
void ResizeRenderer(RendererState& renderer, UINT width, UINT height);
bool UpdateChunkMeshes(RendererState& renderer, World& world);
void UpdateSelectionMesh(RendererState& renderer, const Int3* block);
bool UpdateHudMesh(RendererState& renderer, float fps,
                   const DirectX::XMFLOAT3& position, int block_id);
void RenderFrame(RendererState& renderer, const World& world,
                 const CameraState& camera);
