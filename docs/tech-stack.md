# devy-fps Tech Stack

## Core Runtime
- Language: C++20
- Build system: CMake (>= 3.20)
- Graphics: OpenGL + glad
- Window/input: SDL2
- Physics: Bullet
- Math/data: glm, nlohmann_json
- Networking: ENet
- Optional UI: Dear ImGui

## Build and Tooling
- Dependency manager: vcpkg manifest mode (`vcpkg.json`)
- Deterministic configure/build/test entry points: `CMakePresets.json`
- Warning policy: `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion` (or MSVC `/W4`)
- Zero-warning gate (opt-in): `DEVY_WARNINGS_AS_ERRORS=ON`
- Static analysis (opt-in): `DEVY_ENABLE_CLANG_TIDY=ON` with `.clang-tidy`
- Formatting conventions: `.clang-format`

## Test Stack
- Unit tests: Catch2 (`Catch2::Catch2WithMain`)
- Test runner: CTest
- Smoke test: `server.smoke` (`devy_server config/server_test.json --smoke-seconds 1`)

## Scripts
- `scripts/configure.sh [debug|release|debug-vcpkg|release-vcpkg]`
- `scripts/build.sh [debug|release|debug-vcpkg|release-vcpkg]`
- `scripts/test.sh [debug|release|debug-vcpkg|release-vcpkg]`
- `scripts/smoke-server.sh [config-path] [seconds]`
- `scripts/profile-load.sh [config-path] [clients] [seconds] [out-dir]`
- `scripts/watchdog-server.sh [config-path] [run-seconds] [max-restarts] [backoff-seconds] [out-dir] [rotation-keep]`
- `scripts/chaos-drill.sh [config-path] [clients] [seconds] [out-dir] [malformed-rate-hz] [disconnect-interval-ms] [malformed-family] [malformed-burst-size]`
- `scripts/restart-recovery.sh [config-path] [clients] [phase-seconds] [out-dir]`
- `scripts/reliability-soak.sh [config-path] [minutes] [clients] [out-dir] [chaos-seconds] [restart-phase-seconds] [restart-every-cycles]`
- `scripts/launch-profile.sh [profile] [server|client|both] [preset-or-bin-root]`
- `scripts/generate-release-notes.sh [from-ref] [to-ref] [out-file]`
- `scripts/install-package-smoke.sh [preset] [out-dir] [smoke-seconds] [clients] [load-seconds]`
- `scripts/protocol-compat-check.sh [config-path] [clients] [seconds] [out-dir] [malformed-rate-hz] [malformed-burst-size]`
- `scripts/rollback-rehearsal.sh [preset] [out-dir] [clients] [phase-seconds]`
