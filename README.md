# Devy FPS

Voxel FPS with building, guns, treasure scoring, and time-boxed matches. This repo contains a custom C++ engine and a client/server split.

## Build (Linux)
1. Install dependencies using vcpkg (manifest mode):
   - `./vcpkg install`
2. Configure with CMake:
   - `cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake`
3. Build:
   - `cmake --build build -j`

### Preset-based Build (Recommended)
Set `VCPKG_ROOT` to your vcpkg checkout, then:
- `scripts/configure.sh debug`
- `scripts/build.sh debug`
- `scripts/test.sh debug`

If `VCPKG_ROOT` is unset, the scripts auto-detect `./vcpkg` and `../vcpkg`.

Or use raw preset commands:
- System toolchain: `cmake --preset debug`
- vcpkg toolchain: `cmake --preset debug-vcpkg`
- Build: `cmake --build --preset debug` or `cmake --build --preset debug-vcpkg`
- Test: `ctest --preset debug` or `ctest --preset debug-vcpkg`

## Run
- Client: `./build/client/devy_client`
- Server: `./build/server/devy_server`
- Preset builds output under `build/presets/<preset>/` (for example `build/presets/debug/server/devy_server`).
- Launch profile helper: `scripts/launch-profile.sh [profile] [server|client|both] [preset-or-bin-root]`
  - Profiles: `profiles/launch/local-dev.env`, `profiles/launch/release-candidate.env`, `profiles/launch/canary.env`

You can pass a config path to either executable:
- `./build/client/devy_client config/server_120.json`
- `./build/server/devy_server config/server_120.json`
- `./build/server/devy_server config/server_test.json --smoke-seconds 2`

## Testing
- Unit tests: `shared.unit` (Catch2)
- Unit tests: `client.unit` (Catch2 client prediction/reconciliation coverage)
- Unit tests: `server.unit` (Catch2 server runtime coverage: session lifecycle + authoritative loop + movement simulation + block interaction + world replication)
- Server smoke test: `server.smoke` (headless ENet boot + timed shutdown)
- Invalid-config boot tests:
  - `server.config.invalid_tick_rate`
  - `server.config.invalid_loot_drop`
  - `server.config.invalid_json`
- Telemetry/alert dry-run test:
  - `server.telemetry.alert_dry_run`
- Active-session load profile + telemetry artifact capture: `scripts/profile-load.sh [config-path] [clients] [seconds] [out-dir]`
- Telemetry diagnostics dry run: `scripts/diagnostics-dry-run.sh [config-path] [seconds] [out-dir] [expected-alert]`
- CI validation helpers:
  - cache correctness: `scripts/ci-cache-check.sh [preset]`
  - pipeline failure injection: `scripts/ci-failure-injection.sh [server-binary] [invalid-config] [expected-message]`
  - deterministic packaging: `scripts/package-artifacts.sh [preset] [out-dir]`
  - reproducible hash comparator: `scripts/verify-artifact-repro.sh [preset] [out-dir] [skip-build(0|1)]`
  - fresh-install packaged smoke check: `scripts/install-package-smoke.sh [preset] [out-dir] [smoke-seconds] [clients] [load-seconds]`
  - protocol upgrade/downgrade compatibility check: `scripts/protocol-compat-check.sh [config-path] [clients] [seconds] [out-dir] [malformed-rate-hz] [malformed-burst-size]`
  - rollback rehearsal (candidate deploy + rollback): `scripts/rollback-rehearsal.sh [preset] [out-dir] [clients] [phase-seconds]`
  - release notes generator: `scripts/generate-release-notes.sh [from-ref] [to-ref] [out-file]`
  - alpha multiplayer acceptance pack: `scripts/alpha-acceptance-pack.sh [config-path] [out-dir] [preset] [clients] [scenario-seconds] [run-regression(0|1)]`
  - alpha endurance evidence runner (8h target): `scripts/alpha-endurance-run.sh [config-path] [minutes] [clients] [out-dir] [chaos-seconds] [restart-phase-seconds] [restart-every-cycles]`
  - alpha endurance status helper (live/summary view): `scripts/alpha-endurance-status.sh [out-dir]`
  - alpha release gate orchestrator: `scripts/alpha-release-gate.sh [version-tag] [config-path] [preset] [clients] [scenario-seconds] [endurance-minutes] [out-dir] [run-endurance(0|1)] [from-ref]`
  - set `DEVY_SKIP_BUILD=1` to package from an already-built preset without rebuilding.
- Reliability drills:
  - watchdog restart loop: `scripts/watchdog-server.sh [config-path] [run-seconds] [max-restarts] [backoff-seconds] [out-dir] [rotation-keep]`
  - malformed/flood + disconnect churn: `scripts/chaos-drill.sh [config-path] [clients] [seconds] [out-dir] [malformed-rate-hz] [disconnect-interval-ms] [malformed-family] [malformed-burst-size]`
  - forced restart recovery: `scripts/restart-recovery.sh [config-path] [clients] [phase-seconds] [out-dir]`
  - longer-duration reliability soak runner (chaos + restart loops + trend summary): `scripts/reliability-soak.sh [config-path] [minutes] [clients] [out-dir] [chaos-seconds] [restart-phase-seconds] [restart-every-cycles]`

## CI/CD (Current)
- `.github/workflows/ci.yml`:
  - matrix builds (`debug-vcpkg`, `release-vcpkg`),
  - sharded test lanes (`shared`, `client`, `server`, integration smoke/config/telemetry),
  - cache correctness + failure-injection verification,
  - deterministic release package generation + reproducible artifact hash checks.
- `.github/workflows/reliability.yml`:
  - reliability drill lane for `server.reliability.*` checks,
  - nightly/manual reliability soak lane with artifact retention policy.

## Release Ops (Current)
- Release config templates are under `config/templates/`:
  - `server_release_candidate.json`
  - `server_canary.json`
- Rollback runbook: `docs/rollback-strategy.md`
- New CTest checks for release operations:
  - `server.release.install_smoke`
  - `server.release.protocol_upgrade_downgrade`
  - `server.release.rollback_rehearsal`
  - `server.release.alpha_acceptance_pack`
  - `server.release.alpha_endurance_short`
- Alpha gate docs:
  - `docs/releases/alpha-bug-bash-checklist.md`
  - `docs/releases/alpha-known-issues.md`
  - `docs/releases/alpha-acceptance-checklist.md`
  - `docs/releases/alpha-release-tag-flow.md`

## Networking Handshake (Current)
- Server admission now requires a valid `join_request` packet; raw ENet connect does not auto-admit a player.
- Session liveness is tracked via heartbeat/activity timestamps and server-side timeout eviction.
- Tune timeout with `session.heartbeat_timeout_ms` in server config JSON.

## Authoritative Runtime (Current)
- Server runs a fixed-rate authoritative tick loop (`runtime.tick_rate_hz`).
- Incoming `player_input` packets are queued with sequence-order validation and bounded by `runtime.input_queue_capacity`.
- Server movement simulation applies normalized per-player input at configurable speed (`runtime.movement_speed_units_per_second`).
- Server runtime profiling emits periodic baseline metrics (`runtime.profiling`):
  - tick cost summary (`avg/p95/max`),
  - phase timing split (movement/combat/inventory/lifecycle/broadcast/snapshot),
  - outbound packet counts/bytes by message type,
  - peak runtime state-size estimate from active sessions/queues/events/spawns.
- Server runtime diagnostics now emit explicit operational counters (`runtime.profiling`):
  - tick lag count/rate versus configured tick budget (`tick_lag_tolerance_ms`),
  - inbound packet drop and parse-error rates,
  - command rejection counts and active-player averages,
  - machine-parseable `Runtime diagnostics json=...` log lines with optional alert thresholds (`runtime.profiling.alerts.*`).
- Server sends reliable `match_state` packets on state transitions by default to trim per-snapshot
  network overhead; set `runtime.match_state_broadcast_on_snapshot=true` to restore legacy
  every-snapshot reliable `match_state` broadcasts.
- Server startup now validates config schema/types/ranges and fails fast on invalid values instead
  of silently clamping them; valid `loot_drop` values are `all` and `none`.
- Server startup now also fails fast on config file load/parse errors instead of silently falling
  back to defaults.
- `devy_load_client` now supports chaos-load flags for reliability drills:
  - `--malformed-rate-hz` to inject protocol-invalid packets over ENet,
  - `--malformed-family` to choose malformed message family (`legacy`, `mixed`, `invalid_json`, `unsupported_version`, `unknown_type`, `schema`, `envelope`),
  - `--malformed-burst-size` to emit multiple malformed packets per interval tick,
  - `--disconnect-interval-ms` and `--reconnect-delay-ms` for forced disconnect/reconnect churn.
- Snapshot `match_state` payloads omit scoreboard by default to reduce per-snapshot bytes;
  set `runtime.snapshot_include_match_scoreboard=true` to restore scoreboard-in-snapshot behavior.
- Snapshot broadcasts run on tick cadence (`runtime.snapshot_interval_ticks`) using protocol `state_snapshot` and include:
  - player position/velocity state,
  - `last_processed_input_seq` acknowledgement for client reconciliation scaffolding.
- Server snapshots now also include `chunk_sync` replication fields (`added`, `removed`, `deltas`) per recipient using distance-based chunk relevance culling.
- Server now processes authoritative `block_update` requests (break/place), validates conflicts/ranges, and marks chunk revisions for replication deltas.
- Client-side prediction/reconciliation scaffolding is implemented in `client_runtime` (`PredictionReconciler`) for replaying unacknowledged local inputs from authoritative ack snapshots.
- Client-side `weapon_fire` packet emission scaffolding is implemented in `client_runtime` (`WeaponFireEmitter`) with monotonic shot sequencing and normalized aim vectors.

## Controls (Current)
- WASD: move
- Mouse: look
- Left click: emit local `weapon_fire` request scaffold (logged)
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
- Server-side world replication scaffolding is implemented, but the interactive client still renders a local preview region via `map.preview_chunks`.
- Terrain collision is heightmap-based (no caves yet).
