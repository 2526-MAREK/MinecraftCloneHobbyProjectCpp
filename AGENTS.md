# AGENTS.md

This repo contains two parallel prototypes:
- `src/` + `CMakeLists.txt`: raylib-based C++23 prototype.
- `dx11/`: native Visual Studio 2022 DirectX 11 prototype.

## Quick Build/Run

Raylib (CMake):
1. Open repo root in VS 2022 ("Open a local folder").
2. Configure CMake.
3. Build/Run target `minecraft_clone`.

DirectX 11 (native .sln):
1. Open `dx11/MinecraftCloneDX11.sln`.
2. Build `Debug | x64`.
3. Run (F5).

## Current DX11 Features
- Chunk streaming around the camera (multiple 16x16x16 chunks).
- Face-culling mesh generation with cross-chunk neighbor checks.
- Per-chunk frustum culling.
- Texture atlas loaded from `dx11/assets/atlas.ppm` + per-face UVs.
- FPS camera (WASD + mouse, Space/Shift up/down, Ctrl speed).
- Block break/place with LMB/RMB.
- Hover highlight (wireframe selection box).
- Crosshair + basic HUD (FPS, position, block ID).

## Key Files (DX11)
- `dx11/src/main.cpp`: app entry point + high-level loop.
- `dx11/src/renderer.cpp`: DirectX 11 device/pipeline, mesh upload, HUD/selection.
- `dx11/src/world.cpp`: voxel world data, chunk streaming, raycast.
- `dx11/src/input.cpp`: mouse capture + keyboard/mouse state.
- `dx11/src/camera.cpp`: FPS camera movement + look.
- `dx11/MinecraftCloneDX11.sln`: Visual Studio solution.
- `dx11/MinecraftCloneDX11.vcxproj`: project settings.

## Next Steps (Suggested)
1. Load textures from file via PNG + WIC (replace PPM loader).
2. Implement greedy meshing for fewer triangles.
3. Add collision, gravity, and basic player controller.
4. Add chunk LOD or async generation for larger worlds.

## Guidelines for Changes
- Keep DX11 prototype self-contained under `dx11/`.
- Prefer C++23 and MSVC v143 settings.
- Avoid non-ASCII unless the file already uses it.
- When changing rendering, keep the basic demo runnable.

## Testing
- No automated tests yet.
- Verify by building and running the DX11 solution and the CMake raylib target.
