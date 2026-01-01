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
- Chunked voxel mesh (single 16x16x16 chunk with flat ground).
- Face-culling mesh generation.
- Texture atlas generated in code + per-face UVs.
- FPS camera (WASD + mouse, Space/Shift up/down, Ctrl speed).
- Block break/place with LMB/RMB.
- Hover highlight (wireframe selection box).

## Key Files (DX11)
- `dx11/src/main.cpp`: all DX11 rendering, input, voxel logic, and shaders.
- `dx11/MinecraftCloneDX11.sln`: Visual Studio solution.
- `dx11/MinecraftCloneDX11.vcxproj`: project settings.

## Next Steps (Suggested)
1. Load textures from file instead of in-code atlas (e.g. PNG + WIC loader).
2. Add chunk streaming (multiple chunks, world origin, frustum culling).
3. Separate systems into modules (renderer, world, input, camera).
4. Implement greedy meshing for fewer triangles.
5. Add collision, gravity, and basic player controller.
6. Add UI overlay (crosshair, FPS, block type).

## Guidelines for Changes
- Keep DX11 prototype self-contained under `dx11/`.
- Prefer C++23 and MSVC v143 settings.
- Avoid non-ASCII unless the file already uses it.
- When changing rendering, keep the basic demo runnable.

## Testing
- No automated tests yet.
- Verify by building and running the DX11 solution and the CMake raylib target.
