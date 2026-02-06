# Devy FPS

Voxel FPS with building, guns, treasure scoring, and time-boxed matches. This repo contains a custom C++ engine and a client/server split.

## Build (Linux)
1. Install dependencies using vcpkg (manifest mode):
   - `./vcpkg install`
2. Configure with CMake:
   - `cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake`
3. Build:
   - `cmake --build build -j`

## Run
- Client: `./build/client/devy_client`
- Server: `./build/server/devy_server`

You can pass a config path to either executable:
- `./build/client/devy_client config/server_120.json`
- `./build/server/devy_server config/server_120.json`

## Controls (Current)
- WASD: move
- Mouse: look
- Shift: sprint
- C: crouch
- Space: jump
- Shift + C: slide
- Esc: quit

## Rendering Notes
- Frustum + distance culling is enabled (see `map.draw_distance_chunks`).
- Simple directional + ambient lighting in voxel shader.

## Repo Layout
- `engine/` custom engine (SDL2 + OpenGL + Bullet)
- `shared/` shared gameplay, voxel, and protocol code
- `client/` FPS client
- `server/` authoritative server
- `config/` JSON configs for balance and match rules

## Match Defaults
- 60 minutes, 4 km^2 map, 64 max players
- 120 minutes, 8 km^2 map (see `config/server_120.json`)
- 2 respawns
- Loot drops on death

## Notes
- World streaming is not implemented yet, so the client generates a preview region via `map.preview_chunks`.
- Terrain collision is heightmap-based (no caves yet).
