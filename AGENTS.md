# Project: devy-fps

## Tech Stack
- **Core:** C++20, CMake, SDL2, OpenGL, Bullet, ENet, glm, nlohmann_json, Dear ImGui
- **Env:** Fedora / Fish / Neovim

## Autonomous Workflow
- **Initialization:** Read `docs/` to restore mental model.
- **Sync Routine:** 1. Update `docs/`
  2. `git add docs/ && git commit -m "docs: sync project state"`
  3. `git add . && git commit -m "feat/fix: [desc]"`

## Agent Commands
- **/context**: `cat docs/project-status.md docs/decision-log.md docs/architecture.md`
- **/status**: `cat docs/project-status.md`
- **/history**: `tail -n 20 docs/decision-log.md`

## Build/Test Scripts
- `scripts/configure.sh [debug|release]`
- `scripts/build.sh [debug|release]`
- `scripts/test.sh [debug|release]`
- `scripts/smoke-server.sh [config-path] [seconds]`
- `scripts/profile-load.sh [config-path] [clients] [seconds] [out-dir]`
- `scripts/chaos-drill.sh [config-path] [clients] [seconds] [out-dir] [malformed-rate-hz] [disconnect-interval-ms] [malformed-family] [malformed-burst-size]`
- `scripts/restart-recovery.sh [config-path] [clients] [phase-seconds] [out-dir]`
- `scripts/watchdog-server.sh [config-path] [run-seconds] [max-restarts] [backoff-seconds] [out-dir] [rotation-keep]`
- `scripts/reliability-soak.sh [config-path] [minutes] [clients] [out-dir] [chaos-seconds] [restart-phase-seconds] [restart-every-cycles]`
- `scripts/alpha-acceptance-pack.sh [config-path] [out-dir] [preset] [clients] [scenario-seconds] [run-regression(0|1)]`
- `scripts/alpha-endurance-run.sh [config-path] [minutes] [clients] [out-dir] [chaos-seconds] [restart-phase-seconds] [restart-every-cycles]`
- `scripts/alpha-endurance-status.sh [out-dir]`
- `scripts/alpha-release-gate.sh [version-tag] [config-path] [preset] [clients] [scenario-seconds] [endurance-minutes] [out-dir] [run-endurance(0|1)] [from-ref]`
- `scripts/alpha-post-endurance-followup.sh [endurance-out-dir] [version-tag] [config-path] [preset] [clients] [scenario-seconds] [endurance-minutes] [from-ref] [poll-seconds] [out-dir] [run-gate-endurance(0|1)]`
- `scripts/alpha-finalize-release.sh [followup-out-dir] [version-tag] [from-ref] [poll-seconds] [out-dir] [create-tag(0|1)]`
