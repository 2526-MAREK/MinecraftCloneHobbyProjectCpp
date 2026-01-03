
#include "renderer.h"

#include <d3dcompiler.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <cmath>
#include <string>
#include <vector>

namespace {
using D3DCompileFn =
    HRESULT(WINAPI*)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*,
                     LPCSTR, LPCSTR, UINT, UINT, ID3DBlob**, ID3DBlob**);

HMODULE g_d3dcompiler_module = nullptr;
D3DCompileFn g_d3d_compile = nullptr;

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
  HRESULT hr = g_d3d_compile(source, std::strlen(source), nullptr, nullptr,
                             nullptr, entry, target, flags, 0, &shader_blob,
                             &error_blob);
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
  if (max_value <= 0) {
    return false;
  }

  pixels.clear();
  pixels.reserve(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
  int value = 0;
  for (int i = 0; i < width * height * 3; ++i) {
    if (!NextToken(file, token)) {
      return false;
    }
    value = static_cast<int>(std::strtol(token.c_str(), nullptr, 10));
    const uint8_t channel =
        static_cast<uint8_t>(std::clamp(value, 0, max_value) * 255 / max_value);
    pixels.push_back(channel);
    if ((i % 3) == 2) {
      pixels.push_back(255);
    }
  }
  return true;
}

bool CreateTextureAtlas(RendererState& renderer) {
  std::vector<uint8_t> pixels;
  int width = 0;
  int height = 0;
  if (!LoadPPMFile("assets/atlas.ppm", pixels, width, height)) {
    MessageBoxA(nullptr, "Failed to load assets/atlas.ppm.",
                "Texture Error", MB_ICONERROR);
    return false;
  }

  if (width <= 0 || height <= 0) {
    MessageBoxA(nullptr, "Invalid texture size.",
                "Texture Error", MB_ICONERROR);
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
  HRESULT hr = renderer.device->CreateTexture2D(&desc, &init_data, &texture);
  if (FAILED(hr)) {
    ShowError("Failed to create texture atlas", hr);
    return false;
  }

  D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc{};
  srv_desc.Format = desc.Format;
  srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
  srv_desc.Texture2D.MipLevels = 1;

  hr = renderer.device->CreateShaderResourceView(texture.Get(), &srv_desc,
                                                 &renderer.texture_srv);
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

  hr = renderer.device->CreateSamplerState(&sampler_desc,
                                           &renderer.sampler_state);
  if (FAILED(hr)) {
    ShowError("Failed to create sampler state", hr);
    return false;
  }

  return true;
}

bool CreatePipeline(RendererState& renderer) {
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
      const float tileIndex = input.color.a;
      const float2 tileSize = float2(1.0f / 4.0f, 1.0f / 1.0f);
      const float tileX = fmod(tileIndex, 4.0f);
      const float tileY = floor(tileIndex / 4.0f);
      const float2 base = float2(tileX, tileY) * tileSize;
      const float2 uv = base + frac(input.uv) * tileSize;
      return atlas.Sample(atlasSampler, uv) * float4(input.color.rgb, 1.0f);
    }
  )";

  Microsoft::WRL::ComPtr<ID3DBlob> vs_blob;
  if (!CompileShader(vs_source, "main", "vs_5_0", vs_blob)) {
    return false;
  }
  HRESULT hr = renderer.device->CreateVertexShader(
      vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), nullptr,
      &renderer.vertex_shader);
  if (FAILED(hr)) {
    ShowError("Failed to create vertex shader", hr);
    return false;
  }

  Microsoft::WRL::ComPtr<ID3DBlob> ps_blob;
  if (!CompileShader(ps_source, "main", "ps_5_0", ps_blob)) {
    return false;
  }
  hr = renderer.device->CreatePixelShader(
      ps_blob->GetBufferPointer(), ps_blob->GetBufferSize(), nullptr,
      &renderer.pixel_shader);
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
  hr = renderer.device->CreatePixelShader(
      solid_ps_blob->GetBufferPointer(), solid_ps_blob->GetBufferSize(), nullptr,
      &renderer.solid_pixel_shader);
  if (FAILED(hr)) {
    ShowError("Failed to create solid pixel shader", hr);
    return false;
  }

  const D3D11_INPUT_ELEMENT_DESC layout[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
       static_cast<UINT>(offsetof(Vertex, position)),
       D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
       static_cast<UINT>(offsetof(Vertex, color)), D3D11_INPUT_PER_VERTEX_DATA,
       0},
      {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0,
       static_cast<UINT>(offsetof(Vertex, uv)), D3D11_INPUT_PER_VERTEX_DATA, 0},
  };
  hr = renderer.device->CreateInputLayout(
      layout, static_cast<UINT>(sizeof(layout) / sizeof(layout[0])),
      vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(),
      &renderer.input_layout);
  if (FAILED(hr)) {
    ShowError("Failed to create input layout", hr);
    return false;
  }

  D3D11_BUFFER_DESC constant_desc{};
  constant_desc.ByteWidth = sizeof(DirectX::XMFLOAT4X4);
  constant_desc.Usage = D3D11_USAGE_DEFAULT;
  constant_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  hr = renderer.device->CreateBuffer(&constant_desc, nullptr,
                                     &renderer.constant_buffer);
  if (FAILED(hr)) {
    ShowError("Failed to create constant buffer", hr);
    return false;
  }

  D3D11_RASTERIZER_DESC raster_desc{};
  raster_desc.FillMode = D3D11_FILL_SOLID;
  raster_desc.CullMode = D3D11_CULL_NONE;
  raster_desc.DepthClipEnable = TRUE;
  hr = renderer.device->CreateRasterizerState(&raster_desc,
                                              &renderer.rasterizer_state);
  if (FAILED(hr)) {
    ShowError("Failed to create rasterizer state", hr);
    return false;
  }

  D3D11_DEPTH_STENCIL_DESC depth_desc{};
  depth_desc.DepthEnable = TRUE;
  depth_desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
  depth_desc.DepthFunc = D3D11_COMPARISON_LESS;
  hr = renderer.device->CreateDepthStencilState(&depth_desc,
                                                &renderer.depth_state);
  if (FAILED(hr)) {
    ShowError("Failed to create depth stencil state", hr);
    return false;
  }

  depth_desc.DepthEnable = FALSE;
  depth_desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
  depth_desc.DepthFunc = D3D11_COMPARISON_ALWAYS;
  hr = renderer.device->CreateDepthStencilState(&depth_desc,
                                                &renderer.depth_state_no_depth);
  if (FAILED(hr)) {
    ShowError("Failed to create HUD depth stencil state", hr);
    return false;
  }

  D3D11_RASTERIZER_DESC wire_desc = raster_desc;
  wire_desc.FillMode = D3D11_FILL_WIREFRAME;
  wire_desc.CullMode = D3D11_CULL_NONE;
  hr = renderer.device->CreateRasterizerState(&wire_desc,
                                              &renderer.wireframe_state);
  if (FAILED(hr)) {
    ShowError("Failed to create wireframe rasterizer state", hr);
    return false;
  }

  if (!CreateTextureAtlas(renderer)) {
    return false;
  }

  renderer.vertex_stride = sizeof(Vertex);
  renderer.vertex_offset = 0;
  return true;
}
bool CreateRenderTarget(RendererState& renderer) {
  Microsoft::WRL::ComPtr<ID3D11Texture2D> back_buffer;
  HRESULT hr =
      renderer.swap_chain->GetBuffer(0, IID_PPV_ARGS(&back_buffer));
  if (FAILED(hr)) {
    ShowError("Failed to get swap chain back buffer", hr);
    return false;
  }
  hr = renderer.device->CreateRenderTargetView(back_buffer.Get(), nullptr,
                                               &renderer.render_target);
  if (FAILED(hr)) {
    ShowError("Failed to create render target view", hr);
    return false;
  }

  D3D11_TEXTURE2D_DESC depth_desc{};
  depth_desc.Width = renderer.width;
  depth_desc.Height = renderer.height;
  depth_desc.MipLevels = 1;
  depth_desc.ArraySize = 1;
  depth_desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
  depth_desc.SampleDesc.Count = 1;
  depth_desc.Usage = D3D11_USAGE_DEFAULT;
  depth_desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
  hr = renderer.device->CreateTexture2D(&depth_desc, nullptr,
                                        &renderer.depth_buffer);
  if (FAILED(hr)) {
    ShowError("Failed to create depth buffer", hr);
    return false;
  }
  hr = renderer.device->CreateDepthStencilView(
      renderer.depth_buffer.Get(), nullptr, &renderer.depth_stencil_view);
  if (FAILED(hr)) {
    ShowError("Failed to create depth stencil view", hr);
    return false;
  }

  renderer.context->OMSetRenderTargets(
      1, renderer.render_target.GetAddressOf(),
      renderer.depth_stencil_view.Get());
  D3D11_VIEWPORT viewport{};
  viewport.Width = static_cast<float>(renderer.width);
  viewport.Height = static_cast<float>(renderer.height);
  viewport.MinDepth = 0.0f;
  viewport.MaxDepth = 1.0f;
  viewport.TopLeftX = 0.0f;
  viewport.TopLeftY = 0.0f;
  renderer.context->RSSetViewports(1, &viewport);
  return true;
}

bool UploadChunkMesh(RendererState& renderer, ChunkMesh& mesh,
                     const std::vector<Vertex>& vertices) {
  if (!renderer.device || !renderer.context) {
    return false;
  }
  if (vertices.empty()) {
    mesh.vertex_count = 0;
    return true;
  }
  mesh.vertex_count = static_cast<UINT>(vertices.size());
  const UINT byte_size = mesh.vertex_count * sizeof(Vertex);
  if (!mesh.vertex_buffer || byte_size > mesh.vertex_buffer_size) {
    mesh.vertex_buffer.Reset();
    D3D11_BUFFER_DESC buffer_desc{};
    buffer_desc.ByteWidth = byte_size;
    buffer_desc.Usage = D3D11_USAGE_DYNAMIC;
    buffer_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    HRESULT hr = renderer.device->CreateBuffer(&buffer_desc, nullptr,
                                               &mesh.vertex_buffer);
    if (FAILED(hr)) {
      ShowError("Failed to create voxel vertex buffer", hr);
      return false;
    }
    mesh.vertex_buffer_size = byte_size;
  }

  D3D11_MAPPED_SUBRESOURCE mapped{};
  HRESULT hr = renderer.context->Map(mesh.vertex_buffer.Get(), 0,
                                     D3D11_MAP_WRITE_DISCARD, 0, &mapped);
  if (FAILED(hr)) {
    ShowError("Failed to map voxel vertex buffer", hr);
    return false;
  }
  std::memcpy(mapped.pData, vertices.data(), byte_size);
  renderer.context->Unmap(mesh.vertex_buffer.Get(), 0);
  return true;
}

bool UploadSelectionMesh(RendererState& renderer,
                         const std::vector<Vertex>& vertices) {
  if (!renderer.device || !renderer.context) {
    return false;
  }
  if (vertices.empty()) {
    renderer.highlight_vertex_count = 0;
    return true;
  }
  renderer.highlight_vertex_count = static_cast<UINT>(vertices.size());
  const UINT byte_size = renderer.highlight_vertex_count * sizeof(Vertex);
  if (!renderer.highlight_vertex_buffer ||
      byte_size > renderer.highlight_vertex_buffer_size) {
    renderer.highlight_vertex_buffer.Reset();
    D3D11_BUFFER_DESC buffer_desc{};
    buffer_desc.ByteWidth = byte_size;
    buffer_desc.Usage = D3D11_USAGE_DYNAMIC;
    buffer_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    HRESULT hr = renderer.device->CreateBuffer(&buffer_desc, nullptr,
                                               &renderer.highlight_vertex_buffer);
    if (FAILED(hr)) {
      ShowError("Failed to create selection vertex buffer", hr);
      return false;
    }
    renderer.highlight_vertex_buffer_size = byte_size;
  }

  D3D11_MAPPED_SUBRESOURCE mapped{};
  HRESULT hr = renderer.context->Map(renderer.highlight_vertex_buffer.Get(), 0,
                                     D3D11_MAP_WRITE_DISCARD, 0, &mapped);
  if (FAILED(hr)) {
    ShowError("Failed to map selection vertex buffer", hr);
    return false;
  }
  std::memcpy(mapped.pData, vertices.data(), byte_size);
  renderer.context->Unmap(renderer.highlight_vertex_buffer.Get(), 0);
  return true;
}

bool UploadHudMesh(RendererState& renderer, const std::vector<Vertex>& vertices) {
  if (!renderer.device || !renderer.context) {
    return false;
  }
  if (vertices.empty()) {
    renderer.hud_vertex_count = 0;
    return true;
  }
  renderer.hud_vertex_count = static_cast<UINT>(vertices.size());
  const UINT byte_size = renderer.hud_vertex_count * sizeof(Vertex);
  if (!renderer.hud_vertex_buffer ||
      byte_size > renderer.hud_vertex_buffer_size) {
    renderer.hud_vertex_buffer.Reset();
    D3D11_BUFFER_DESC buffer_desc{};
    buffer_desc.ByteWidth = byte_size;
    buffer_desc.Usage = D3D11_USAGE_DYNAMIC;
    buffer_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    HRESULT hr = renderer.device->CreateBuffer(&buffer_desc, nullptr,
                                               &renderer.hud_vertex_buffer);
    if (FAILED(hr)) {
      ShowError("Failed to create HUD vertex buffer", hr);
      return false;
    }
    renderer.hud_vertex_buffer_size = byte_size;
  }

  D3D11_MAPPED_SUBRESOURCE mapped{};
  HRESULT hr = renderer.context->Map(renderer.hud_vertex_buffer.Get(), 0,
                                     D3D11_MAP_WRITE_DISCARD, 0, &mapped);
  if (FAILED(hr)) {
    ShowError("Failed to map HUD vertex buffer", hr);
    return false;
  }
  std::memcpy(mapped.pData, vertices.data(), byte_size);
  renderer.context->Unmap(renderer.hud_vertex_buffer.Get(), 0);
  return true;
}

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

std::vector<Vertex> BuildHudMesh(RendererState& renderer, float fps,
                                 const DirectX::XMFLOAT3& position,
                                 int block_id) {
  std::vector<Vertex> vertices;
  if (renderer.width == 0 || renderer.height == 0) {
    return vertices;
  }

  const float screen_w = static_cast<float>(renderer.width);
  const float screen_h = static_cast<float>(renderer.height);
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

  std::snprintf(buffer, sizeof(buffer), "FPS:%d",
                static_cast<int>(fps + 0.5f));
  DrawText(vertices, x, y, kHudScale, buffer, white, screen_w, screen_h);
  y += line_height;

  const int pos_x = static_cast<int>(std::floor(position.x));
  const int pos_y = static_cast<int>(std::floor(position.y));
  const int pos_z = static_cast<int>(std::floor(position.z));
  std::snprintf(buffer, sizeof(buffer), "X:%d Y:%d Z:%d", pos_x, pos_y, pos_z);
  DrawText(vertices, x, y, kHudScale, buffer, white, screen_w, screen_h);
  y += line_height;

  std::snprintf(buffer, sizeof(buffer), "B:%d", block_id);
  DrawText(vertices, x, y, kHudScale, buffer, white, screen_w, screen_h);

  return vertices;
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

bool IsChunkVisible(const DirectX::XMMATRIX& view_proj, const Int3& coord) {
  const float size = static_cast<float>(kChunkSize) * kBlockSize;
  const DirectX::XMFLOAT3 min_point{
      coord.x * size,
      coord.y * size,
      coord.z * size,
  };
  const DirectX::XMFLOAT3 max_point{
      min_point.x + size,
      min_point.y + size,
      min_point.z + size,
  };
  return IsAabbVisible(view_proj, min_point, max_point);
}
}  // namespace

bool InitRenderer(RendererState& renderer, HWND hwnd, UINT width, UINT height) {
  renderer.hwnd = hwnd;
  renderer.width = width;
  renderer.height = height;

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
      D3D11_SDK_VERSION, &swap_desc, &renderer.swap_chain, &renderer.device,
      &feature_level, &renderer.context);
  if (FAILED(hr)) {
    ShowError("Failed to create D3D11 device and swap chain", hr);
    return false;
  }

  if (!CreateRenderTarget(renderer)) {
    return false;
  }
  if (!CreatePipeline(renderer)) {
    return false;
  }

  return true;
}

void ShutdownRenderer(RendererState& renderer) {
  if (renderer.context) {
    renderer.context->ClearState();
  }
  if (g_d3dcompiler_module) {
    FreeLibrary(g_d3dcompiler_module);
    g_d3dcompiler_module = nullptr;
    g_d3d_compile = nullptr;
  }
}

void ResizeRenderer(RendererState& renderer, UINT width, UINT height) {
  if (!renderer.swap_chain || width == 0 || height == 0) {
    return;
  }
  renderer.width = width;
  renderer.height = height;
  renderer.context->OMSetRenderTargets(0, nullptr, nullptr);
  renderer.render_target.Reset();
  renderer.depth_stencil_view.Reset();
  renderer.depth_buffer.Reset();
  HRESULT hr = renderer.swap_chain->ResizeBuffers(0, width, height,
                                                  DXGI_FORMAT_UNKNOWN, 0);
  if (FAILED(hr)) {
    ShowError("Failed to resize swap chain buffers", hr);
    return;
  }
  CreateRenderTarget(renderer);
}

bool UpdateChunkMeshes(RendererState& renderer, World& world) {
  std::vector<Int3> to_remove;
  for (const auto& entry : renderer.chunk_meshes) {
    if (world.chunks.find(entry.first) == world.chunks.end()) {
      to_remove.push_back(entry.first);
    }
  }
  for (const Int3& coord : to_remove) {
    renderer.chunk_meshes.erase(coord);
  }

  for (auto& entry : world.chunks) {
    Chunk& chunk = entry.second;
    ChunkMesh& mesh = renderer.chunk_meshes[entry.first];
    if (!chunk.dirty && mesh.vertex_buffer) {
      continue;
    }
    const std::vector<Vertex> vertices = BuildVoxelMesh(world, chunk);
    if (!UploadChunkMesh(renderer, mesh, vertices)) {
      return false;
    }
    chunk.dirty = false;
  }
  return true;
}

void UpdateSelectionMesh(RendererState& renderer, const Int3* block) {
  if (!block) {
    renderer.highlight_vertex_count = 0;
    return;
  }
  const std::vector<Vertex> vertices = BuildSelectionMesh(*block);
  UploadSelectionMesh(renderer, vertices);
}

bool UpdateHudMesh(RendererState& renderer, float fps,
                   const DirectX::XMFLOAT3& position, int block_id) {
  const std::vector<Vertex> vertices =
      BuildHudMesh(renderer, fps, position, block_id);
  return UploadHudMesh(renderer, vertices);
}
void RenderFrame(RendererState& renderer, const World& world,
                 const CameraState& camera) {
  if (!renderer.context || !renderer.render_target || !renderer.swap_chain) {
    return;
  }
  (void)world;
  const float clear_color[4] = {0.18f, 0.28f, 0.45f, 1.0f};
  renderer.context->OMSetRenderTargets(
      1, renderer.render_target.GetAddressOf(),
      renderer.depth_stencil_view.Get());
  if (renderer.depth_state) {
    renderer.context->OMSetDepthStencilState(renderer.depth_state.Get(), 0);
  }
  renderer.context->ClearRenderTargetView(renderer.render_target.Get(),
                                          clear_color);
  if (renderer.depth_stencil_view) {
    renderer.context->ClearDepthStencilView(
        renderer.depth_stencil_view.Get(),
        D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
  }

  if (renderer.vertex_shader && renderer.pixel_shader && renderer.input_layout &&
      renderer.constant_buffer && renderer.texture_srv &&
      renderer.sampler_state) {
    const float aspect =
        (renderer.height == 0)
            ? 1.0f
            : static_cast<float>(renderer.width) /
                  static_cast<float>(renderer.height);
    const DirectX::XMMATRIX world_matrix = DirectX::XMMatrixIdentity();
    const DirectX::XMVECTOR eye = DirectX::XMLoadFloat3(&camera.position);
    const DirectX::XMVECTOR forward = GetCameraForward(camera);
    const DirectX::XMVECTOR up = DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    const DirectX::XMMATRIX view = DirectX::XMMatrixLookToLH(eye, forward, up);
    const DirectX::XMMATRIX proj =
        DirectX::XMMatrixPerspectiveFovLH(DirectX::XMConvertToRadians(60.0f),
                                          aspect, 0.1f, 200.0f);
    const DirectX::XMMATRIX view_proj = view * proj;
    const DirectX::XMMATRIX mvp =
        DirectX::XMMatrixTranspose(world_matrix * view * proj);
    DirectX::XMFLOAT4X4 mvp_matrix;
    DirectX::XMStoreFloat4x4(&mvp_matrix, mvp);
    renderer.context->UpdateSubresource(renderer.constant_buffer.Get(), 0,
                                        nullptr, &mvp_matrix, 0, 0);

    renderer.context->IASetInputLayout(renderer.input_layout.Get());
    renderer.context->IASetPrimitiveTopology(
        D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    renderer.context->VSSetShader(renderer.vertex_shader.Get(), nullptr, 0);
    renderer.context->VSSetConstantBuffers(
        0, 1, renderer.constant_buffer.GetAddressOf());
    renderer.context->PSSetShader(renderer.pixel_shader.Get(), nullptr, 0);
    renderer.context->PSSetShaderResources(0, 1,
                                           renderer.texture_srv.GetAddressOf());
    renderer.context->PSSetSamplers(0, 1,
                                    renderer.sampler_state.GetAddressOf());
    renderer.context->RSSetState(renderer.rasterizer_state.Get());

    for (const auto& entry : renderer.chunk_meshes) {
      const Int3& coord = entry.first;
      const ChunkMesh& mesh = entry.second;
      if (!mesh.vertex_buffer || mesh.vertex_count == 0) {
        continue;
      }
      if (!IsChunkVisible(view_proj, coord)) {
        continue;
      }
      ID3D11Buffer* buffer = mesh.vertex_buffer.Get();
      renderer.context->IASetVertexBuffers(0, 1, &buffer,
                                           &renderer.vertex_stride,
                                           &renderer.vertex_offset);
      renderer.context->Draw(mesh.vertex_count, 0);
    }
  }

  if (renderer.wireframe_state && renderer.solid_pixel_shader &&
      renderer.highlight_vertex_buffer && renderer.highlight_vertex_count > 0) {
    renderer.context->IASetInputLayout(renderer.input_layout.Get());
    renderer.context->IASetPrimitiveTopology(
        D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    renderer.context->IASetVertexBuffers(
        0, 1, renderer.highlight_vertex_buffer.GetAddressOf(),
        &renderer.vertex_stride, &renderer.vertex_offset);
    renderer.context->VSSetShader(renderer.vertex_shader.Get(), nullptr, 0);
    renderer.context->VSSetConstantBuffers(
        0, 1, renderer.constant_buffer.GetAddressOf());
    renderer.context->PSSetShader(renderer.solid_pixel_shader.Get(), nullptr, 0);
    renderer.context->RSSetState(renderer.wireframe_state.Get());
    renderer.context->Draw(renderer.highlight_vertex_count, 0);
  }

  if (renderer.hud_vertex_buffer && renderer.hud_vertex_count > 0 &&
      renderer.solid_pixel_shader && renderer.depth_state_no_depth) {
    const DirectX::XMMATRIX identity = DirectX::XMMatrixIdentity();
    DirectX::XMFLOAT4X4 mvp_matrix;
    DirectX::XMStoreFloat4x4(&mvp_matrix, identity);
    renderer.context->UpdateSubresource(renderer.constant_buffer.Get(), 0,
                                        nullptr, &mvp_matrix, 0, 0);
    renderer.context->OMSetDepthStencilState(
        renderer.depth_state_no_depth.Get(), 0);
    renderer.context->IASetInputLayout(renderer.input_layout.Get());
    renderer.context->IASetPrimitiveTopology(
        D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    renderer.context->IASetVertexBuffers(
        0, 1, renderer.hud_vertex_buffer.GetAddressOf(),
        &renderer.vertex_stride, &renderer.vertex_offset);
    renderer.context->VSSetShader(renderer.vertex_shader.Get(), nullptr, 0);
    renderer.context->VSSetConstantBuffers(
        0, 1, renderer.constant_buffer.GetAddressOf());
    renderer.context->PSSetShader(renderer.solid_pixel_shader.Get(), nullptr, 0);
    renderer.context->RSSetState(renderer.rasterizer_state.Get());
    renderer.context->Draw(renderer.hud_vertex_count, 0);
  }

  renderer.swap_chain->Present(1, 0);
}
