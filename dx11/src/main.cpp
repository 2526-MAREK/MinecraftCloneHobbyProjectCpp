#include <windows.h>

#include "camera.h"
#include "input.h"
#include "player.h"
#include "renderer.h"
#include "world.h"

namespace {
constexpr int kInitialWidth = 1280;
constexpr int kInitialHeight = 720;
constexpr wchar_t kWindowClassName[] = L"MinecraftCloneDX11Window";
constexpr wchar_t kWindowTitle[] = L"Minecraft Clone - DirectX 11";

RendererState g_renderer;
World g_world;
CameraState g_camera = {{0.0f, 0.0f, 0.0f}, 0.0f, 0.0f, kMoveSpeed,
                         kMouseSensitivity};
PlayerState g_player;
InputState g_input;

RayHit g_hover_hit;
bool g_hover_valid = false;
float g_fps = 0.0f;
float g_fps_timer = 0.0f;
int g_fps_samples = 0;

void UpdateFps(float dt) {
  g_fps_timer += dt;
  ++g_fps_samples;
  if (g_fps_timer >= 0.25f) {
    g_fps = static_cast<float>(g_fps_samples) / g_fps_timer;
    g_fps_timer = 0.0f;
    g_fps_samples = 0;
  }
}

void UpdateHoverHit() {
  if (!g_input.mouse_captured) {
    g_hover_valid = false;
    return;
  }

  const DirectX::XMVECTOR forward_vec = GetCameraForward(g_camera);
  DirectX::XMFLOAT3 forward{};
  DirectX::XMStoreFloat3(&forward, forward_vec);
  const DirectX::XMFLOAT3 origin = g_camera.position;
  const RayHit hit = RaycastVoxel(g_world, origin, forward, kRaycastDistance);
  g_hover_valid = hit.hit;
  if (g_hover_valid) {
    g_hover_hit = hit;
  }
}

void RefreshSelectionMesh() {
  if (!g_hover_valid) {
    UpdateSelectionMesh(g_renderer, nullptr);
    return;
  }
  UpdateSelectionMesh(g_renderer, &g_hover_hit.block);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wparam,
                            LPARAM lparam) {
  switch (message) {
    case WM_ACTIVATE:
      HandleWindowActivate(g_input, LOWORD(wparam) != WA_INACTIVE);
      return 0;
    case WM_LBUTTONDOWN:
      HandleLButtonDown(g_input);
      return 0;
    case WM_SIZE: {
      if (wparam != SIZE_MINIMIZED) {
        const UINT width = LOWORD(lparam);
        const UINT height = HIWORD(lparam);
        ResizeRenderer(g_renderer, width, height);
        UpdateClipRect(g_input);
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
    MessageBoxA(nullptr, "Failed to register window class", "Error",
                MB_ICONERROR);
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

  InitInput(g_input, hwnd);
  InitPlayer(g_player, {8.0f, 4.0f, -14.0f});
  g_camera.position = GetPlayerEyePosition(g_player);

  RECT client_rect{};
  GetClientRect(hwnd, &client_rect);
  const UINT width = static_cast<UINT>(client_rect.right - client_rect.left);
  const UINT height = static_cast<UINT>(client_rect.bottom - client_rect.top);
  if (!InitRenderer(g_renderer, hwnd, width, height)) {
    return 0;
  }

  StreamChunks(g_world, g_camera.position);
  UpdateChunkMeshes(g_renderer, g_world);

  SetMouseCaptured(g_input, true);

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
      UpdateInput(g_input);
      UpdateCameraLook(g_camera, g_input);
      UpdatePlayer(g_player, g_world, g_camera, g_input, dt);
      g_camera.position = GetPlayerEyePosition(g_player);
      StreamChunks(g_world, g_camera.position);
      UpdateChunkMeshes(g_renderer, g_world);
      UpdateHoverHit();

      bool changed = false;
      if (g_input.mouse_captured && g_hover_valid &&
          (g_input.lmb_pressed || g_input.rmb_pressed)) {
        changed = HandleBlockInteraction(g_world, g_hover_hit,
                                         g_input.lmb_pressed,
                                         g_input.rmb_pressed);
      }
      if (changed) {
        UpdateChunkMeshes(g_renderer, g_world);
        UpdateHoverHit();
      }

      RefreshSelectionMesh();

      int block_id = -1;
      if (g_hover_valid) {
        block_id = static_cast<int>(
            GetBlock(g_world, g_hover_hit.block.x, g_hover_hit.block.y,
                     g_hover_hit.block.z));
      }
      UpdateHudMesh(g_renderer, g_fps, g_camera.position, block_id);
      RenderFrame(g_renderer, g_world, g_camera);
    }
  }

  SetMouseCaptured(g_input, false);
  ShutdownRenderer(g_renderer);

  return 0;
}
