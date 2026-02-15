# Changelog

## Unreleased

### Added
- `AGENTS.md` project agent bootstrap file.
- `CMakePresets.json` with deterministic debug/release and vcpkg variants.
- `.clang-format` and `.clang-tidy` baselines.
- `tests/` harness with Catch2 unit tests:
  - protocol serialize/deserialize roundtrip and malformed payload fallback.
  - inventory aggregation correctness.
  - match timer tick behavior.
- CTest smoke test target `server.smoke`.
- `config/server_test.json` smoke fixture config.
- Utility scripts:
  - `scripts/configure.sh`
  - `scripts/build.sh`
  - `scripts/test.sh`
  - `scripts/smoke-server.sh`
  - `scripts/profile-load.sh`
- Protocol v1 parsing surface in `shared/net`:
  - envelope version constants (`kProtocolVersion`, supported min/max),
  - `ParseResult` and `ProtocolError` for structured decode failures,
  - message schema contracts with per-message payload field validation.
- Extended protocol test coverage for malformed envelopes, version compatibility, and schema validation behavior.
- Extended protocol test coverage for enriched `state_snapshot` payloads carrying reconciliation fields (`last_processed_input_seq`).
- `server/include/server/SessionManager.h` and `server/src/SessionManager.cpp` for M1.2 lifecycle state management (join/slot/heartbeat/timeout/disconnect).
- `tests/server/SessionManagerTests.cpp` and `server.unit` CTest target covering connect storm capacity, disconnect cleanup, reconnect identity, and stale timeout handling.
- `heartbeat` protocol message contract (`MessageType::Heartbeat`) with parser test coverage.
- Optional server session timeout config (`session.heartbeat_timeout_ms`) in `config/server.json` and `config/server_test.json`.
- `server/include/server/AuthoritativeLoop.h` and `server/src/AuthoritativeLoop.cpp` for M1.3 authoritative tick runtime behavior.
- `server/include/server/MovementSimulation.h` and `server/src/MovementSimulation.cpp` for M2.1 authoritative movement state simulation.
- Runtime-focused `server.unit` coverage in `tests/server/AuthoritativeLoopTests.cpp`:
  - 30-minute scheduler drift horizon,
  - queue overflow rejection,
  - deterministic input drain order,
  - snapshot cadence assertions.
- Runtime-focused `server.unit` coverage in `tests/server/MovementSimulationTests.cpp`:
  - movement axis normalization with deterministic speed integration,
  - newest-input-per-tick application per player,
  - idle velocity clear and player-state removal behavior.
- Runtime config fields in server configs:
  - `runtime.tick_rate_hz`
  - `runtime.snapshot_interval_ticks`
  - `runtime.input_queue_capacity`
  - `runtime.movement_speed_units_per_second`
- `client/include/client/PredictionReconciler.h` and `client/src/PredictionReconciler.cpp` for M2.1 client-side prediction/reconciliation scaffolding.
- `client.unit` CTest target with `tests/client/PredictionReconcilerTests.cpp` coverage:
  - unacked-input replay behavior after authoritative ack snapshots,
  - snapshot payload consume path for `last_processed_input_seq`,
  - latency/loss simulation matrix (`50/100/200ms` x `1/3/5%`) with bounded correction drift assertions.
- `server/include/server/WorldReplication.h` and `server/src/WorldReplication.cpp` for M2.2 world state replication scaffolding (interest culling + chunk subscription deltas).
- `tests/server/WorldReplicationTests.cpp` covering cold-join sync, revision-driven deltas, chunk subscription churn, and distance-culling behavior.
- `server/include/server/BlockInteraction.h` and `server/src/BlockInteraction.cpp` for M2.3 block break/place validation and conflict-resolution flow.
- `tests/server/BlockInteractionTests.cpp` covering valid apply, invalid action rejection, out-of-bounds updates, and same-block conflict races.
- `server/include/server/CombatSimulation.h` and `server/src/CombatSimulation.cpp` for M3.1 authoritative combat fire/hit resolution.
- `tests/server/CombatSimulationTests.cpp` covering deterministic same-tick fire ordering, lethal/death event emission, and delayed projectile hit windows.
- `tests/shared/WeaponsTests.cpp` covering DPS sanity checks for configured weapon tiers.
- `weapon_fire` protocol payload contract (`MessageType::WeaponFire`) with parser coverage in `tests/shared/ProtocolTests.cpp`.
- Optional weapon config tuning fields in `config/weapons.json`:
  - `range_units`
  - `projectile_speed_units_per_second`
- Shared weapon helpers in `shared/game/Weapons`:
  - `weapon_damage_per_shot(...)`
  - `weapon_dps(...)`
- `client/include/client/WeaponFireEmitter.h` and `client/src/WeaponFireEmitter.cpp` for client-side `weapon_fire` packet emission scaffolding.
- `server/include/server/CombatEvents.h` and `server/src/CombatEvents.cpp` for shared combat event serialization to reliable packets and snapshot event payloads.
- `tests/server/CombatIntegrationTests.cpp` covering end-to-end fire request to damage/death broadcast and snapshot reconciliation fields.
- `tests/client/WeaponFireEmitterTests.cpp` covering fire packet emission validation and sequencing.
- `server/include/server/InventoryLootSimulation.h` and `server/src/InventoryLootSimulation.cpp` for M3.2 authoritative inventory/loot runtime (scheduled spawn, pickup validation, inventory limits, drop-on-death).
- `tests/server/InventoryLootSimulationTests.cpp` covering deterministic spawn cadence, duplicate/capacity pickup rejection, and death-drop inventory clearing/spawn behavior.
- `treasure_pickup` protocol payload contract (`MessageType::TreasurePickup`) with parser coverage in `tests/shared/ProtocolTests.cpp`.
- Optional inventory runtime config fields in server configs:
  - `inventory.spawn_interval_ticks`
  - `inventory.max_active_spawns`
  - `inventory.pickup_queue_capacity`
  - `inventory.pickup_radius_units`
  - `inventory.max_items_per_player`
  - `inventory.max_weight_per_player`
  - `inventory.death_drop_spread_units`
- `server/include/server/MatchLifecycleSimulation.h` and `server/src/MatchLifecycleSimulation.cpp` for M3.3 authoritative match lifecycle runtime (pre-match/in-match/post-match transitions, timer authority, respawn budget enforcement, scoreboard aggregation, deterministic winner resolution).
- `tests/server/MatchLifecycleSimulationTests.cpp` covering full lifecycle transitions, respawn exhaustion behavior, timer edge-case handling, and deterministic winner ordering.
- `match` config object in server configs (`config/server.json`, `config/server_test.json`, `config/server_120.json`) with lifecycle controls:
  - `pre_match_seconds`
  - `duration_seconds`
  - `respawns_per_player`
  - `respawn_delay_seconds`
  - `min_players_to_start`
- `server/include/server/RuntimeTelemetry.h` and `server/src/RuntimeTelemetry.cpp` for Phase 4 profiling baseline metrics (tick timings, phase timings, outbound packet bytes/counts, and peak runtime state-size estimates).
- `tests/server/RuntimeTelemetryTests.cpp` covering report cadence, counter reset behavior between windows, and disabled-mode no-op behavior.
- Optional runtime profiling config object in server configs:
  - `runtime.profiling.enabled`
  - `runtime.profiling.report_interval_ticks`
  - `runtime.profiling.history_size_ticks`
  - `runtime.profiling.tick_lag_tolerance_ms`
  - `runtime.profiling.alerts.max_tick_lag_rate`
  - `runtime.profiling.alerts.max_packet_drop_rate`
  - `runtime.profiling.alerts.max_parse_error_rate`
  - `runtime.profiling.alerts.min_active_players`
- `config/server_diagnostics_alert.json` fixture for telemetry alert dry-run validation.
- `scripts/diagnostics-dry-run.sh` to run server telemetry diagnostics and verify machine-parseable health summaries plus expected alerts.
- `tests/scripts/assert-diagnostics-dry-run.sh` and CTest entry `server.telemetry.alert_dry_run`.
- `tools/devy_load_client.cpp` and `devy_load_client` executable for synthetic ENet load generation (join + heartbeat + player_input traffic) during profiling runs.
- Optional runtime snapshot payload knob in server configs:
  - `runtime.snapshot_include_match_scoreboard`
- `server/include/server/MatchStatePayload.h` and `server/src/MatchStatePayload.cpp` for centralized match-state payload/packet serialization shared by reliable and snapshot paths.
- `tests/server/MatchStatePayloadTests.cpp` covering snapshot scoreboard omission behavior and reliable `match_state` scoreboard inclusion behavior.
- `server/include/server/ServerConfigValidation.h` and `server/src/ServerConfigValidation.cpp` for strict boot-time server config schema/type/range validation.
- `tests/server/ServerConfigValidationTests.cpp` covering root/type/range validation behavior.
- Invalid-config fixtures:
  - `config/server_invalid_tick_rate.json`
  - `config/server_invalid_loot_drop.json`
  - `config/server_invalid_json.json`
- `tests/scripts/assert-invalid-config.sh` and CTest entries:
  - `server.config.invalid_tick_rate`
  - `server.config.invalid_loot_drop`
  - `server.config.invalid_json`
- Reliability automation scripts:
  - `scripts/watchdog-server.sh`
  - `scripts/chaos-drill.sh`
  - `scripts/restart-recovery.sh`
  - `scripts/reliability-soak.sh`
- Reliability CTest wrappers:
  - `tests/scripts/assert-watchdog-restart.sh`
  - `tests/scripts/assert-chaos-drill.sh`
  - `tests/scripts/assert-restart-recovery.sh`
  - `tests/scripts/assert-reliability-soak.sh`
- New CTest entries:
  - `server.reliability.watchdog_restart`
  - `server.reliability.chaos_drill`
  - `server.reliability.restart_recovery`
  - `server.reliability.soak_short`
- GitHub Actions reliability workflow: `.github/workflows/reliability.yml` with dedicated
  reliability drill lane and scheduled/manual soak lane.
- GitHub Actions CI workflow: `.github/workflows/ci.yml` with:
  - build matrix lanes (`debug-vcpkg`, `release-vcpkg`),
  - test shard lanes (`shared`, `client`, `server`, integration smoke/config/telemetry),
  - verification lane for cache-correctness and failure-injection checks,
  - release packaging lane with reproducible artifact hash verification.
- CI helper scripts:
  - `scripts/ci-cache-check.sh`
  - `scripts/ci-failure-injection.sh`
  - `scripts/package-artifacts.sh`
  - `scripts/verify-artifact-repro.sh`
- Launch profile and release-ops assets:
  - `scripts/launch-profile.sh`
  - `profiles/launch/local-dev.env`
  - `profiles/launch/release-candidate.env`
  - `profiles/launch/canary.env`
  - `config/templates/server_release_candidate.json`
  - `config/templates/server_canary.json`
- Release delivery scripts:
  - `scripts/generate-release-notes.sh`
  - `scripts/install-package-smoke.sh`
  - `scripts/protocol-compat-check.sh`
  - `scripts/rollback-rehearsal.sh`
  - `scripts/alpha-acceptance-pack.sh`
  - `scripts/alpha-endurance-run.sh`
  - `scripts/alpha-release-gate.sh`
- Rollback runbook: `docs/rollback-strategy.md`.
- Release notes artifact directory docs: `docs/releases/README.md`.
- Alpha release-gate docs:
  - `docs/releases/alpha-bug-bash-checklist.md`
  - `docs/releases/alpha-known-issues.md`
  - `docs/releases/alpha-acceptance-checklist.md`
  - `docs/releases/alpha-release-tag-flow.md`
- Release-operation CTest wrappers:
  - `tests/scripts/assert-install-package-smoke.sh`
  - `tests/scripts/assert-protocol-compat.sh`
  - `tests/scripts/assert-rollback-rehearsal.sh`
  - `tests/scripts/assert-alpha-acceptance-pack.sh`
  - `tests/scripts/assert-alpha-endurance-short.sh`
  - `tests/scripts/assert-release-notes.sh`
- New CTest entries:
  - `server.release.install_smoke`
  - `server.release.protocol_upgrade_downgrade`
  - `server.release.rollback_rehearsal`
  - `server.release.alpha_acceptance_pack`
  - `server.release.alpha_endurance_short`
  - `server.release.release_notes_generation`

### Changed
- `.gitignore` now ignores `/artifacts/` so generated runtime/reliability evidence stays local and does not pollute source commits.
- Top-level `CMakeLists.txt` now enables warning policy options, optional clang-tidy integration, and test subdirectory wiring.
- `server/src/main.cpp` now supports `--smoke-seconds` for deterministic headless runtime checks.
- `README.md` now documents preset/script workflows and test entry points.
- `vcpkg.json` now includes `catch2`.
- `shared/src/net/Protocol.cpp` now serializes `version` in every packet and validates incoming packet envelope + payload schema before accepting messages.
- `server/src/main.cpp` now gates admission on explicit `JoinRequest`, allocates/reclaims session slots, refreshes heartbeats, and evicts stale sessions with disconnect cleanup.
- `server/CMakeLists.txt` now builds a reusable `server_runtime` library and links it into the server executable.
- `tests/CMakeLists.txt` now includes both `shared.unit` and `server.unit` Catch2 executables.
- `server/src/main.cpp` now:
  - parses/validates runtime tick config,
  - enqueues `player_input` into authoritative runtime queue with range/identity checks,
  - applies per-tick authoritative movement simulation,
  - emits cadence-based `state_snapshot` packets to active sessions with position/velocity and `last_processed_input_seq` fields for reconciliation scaffolding,
  - generates server-side world chunks on startup and includes per-recipient `chunk_sync` replication payloads (`added`, `removed`, `deltas`) in snapshots.
- `server/include/server/SessionManager.h` now exposes ordered active-session enumeration used by snapshot broadcast paths.
- `client/CMakeLists.txt` now builds a reusable `client_runtime` library and links it into the client executable.
- `tests/CMakeLists.txt` now includes `client.unit` alongside existing `shared.unit` and `server.unit` targets.
- `tests/CMakeLists.txt` now includes `WorldReplicationTests` in `server.unit`.
- `tests/CMakeLists.txt` now includes `BlockInteractionTests` in `server.unit`.
- `shared/include/shared/voxel/World.h` now exposes a const `get_chunk(...)` overload for read-only replication queries.
- `shared/include/shared/voxel/World.h` now exposes `block_at(...)` and `set_block(...)` helpers for authoritative world mutation by world coordinates.
- `shared/src/net/Protocol.cpp` now accepts optional `player_id` in `block_update` payload schema.
- `server/src/main.cpp` now handles `block_update` requests authoritatively, validates optional identity and coordinate ranges, applies updates via `BlockInteraction`, and marks dirty chunks for replication deltas.
- `server/src/main.cpp` now:
  - loads weapon behavior config for runtime combat authority,
  - handles `weapon_fire` messages with join/identity/payload validation,
  - resolves hitscan and projectile combat outcomes through `CombatSimulation`,
  - broadcasts reliable `damage_event` packets and appends combat events into snapshot payload `events`,
  - includes per-player combat state fields (`health`, `alive`, `last_shot_seq`) in `state_snapshot.players[*]`.
- `tests/CMakeLists.txt` now includes `CombatSimulationTests` in `server.unit` and `WeaponsTests` in `shared.unit`.
- `client/src/main.cpp` now captures left-click fire intent and emits validated local `weapon_fire` requests with aim origin/direction sampling from camera/player state.
- `server/src/main.cpp` now delegates combat packet/event translation to `server::CombatEvents`.
- `tests/CMakeLists.txt` now includes `CombatIntegrationTests` in `server.unit` and `WeaponFireEmitterTests` in `client.unit`.
- `tests/CMakeLists.txt` now includes `InventoryLootSimulationTests` in `server.unit`.
- `shared/src/net/Protocol.cpp` now validates `treasure_pickup` payloads (`player_id`, `pickup_seq`, `spawn_id`).
- `server/src/main.cpp` now:
  - loads treasure definitions and inventory runtime config,
  - handles `treasure_pickup` packets with identity/range validation and enqueue gating,
  - resolves server-authoritative inventory/loot simulation each tick,
  - emits reliable `inventory_update` deltas and snapshot loot events/spawn state (`treasure_spawns`),
  - includes per-player inventory summary fields in `state_snapshot.players[*]`.
- `server/src/CombatSimulation.cpp` now exposes `respawn_player(...)` so lifecycle-authoritative respawns can revive players and clear stale pending projectile hits.
- `server/src/main.cpp` now:
  - loads match lifecycle runtime config (`match` object with fallback to legacy `match_time_minutes`/`respawns`),
  - resolves authoritative match lifecycle state each server tick,
  - gates `weapon_fire` and `treasure_pickup` handling to active match phase (`InMatch`),
  - applies lifecycle respawn outputs into combat state and emits snapshot `respawn_event`/`match_state_changed` events,
  - broadcasts reliable `match_state` packets on state changes and snapshot cadence (plus immediate post-join state),
  - extends snapshot payloads with top-level `match_state` and per-player fields (`respawns_remaining`, `eliminated`).
- `tests/CMakeLists.txt` now includes `MatchLifecycleSimulationTests` in `server.unit`.
- `tests/shared/ProtocolTests.cpp` now includes schema coverage for `match_state` payload validation.
- `server/src/main.cpp` now:
  - parses/validates `runtime.profiling` config,
  - records authoritative tick phase timings (`movement`, `combat`, `inventory`, `match_lifecycle`, `broadcast`, `snapshot_build`, `snapshot_send`),
  - records outbound packet bytes/counts from all reliable send paths,
  - emits periodic runtime profile summaries to logs with tick `avg/p95/max` and peak state pressure signals,
  - emits machine-parseable runtime diagnostics JSON lines with tick lag/drop/parse-error/player-count counters and threshold-derived alerts.
- `server/src/main.cpp` now instruments inbound receive paths to count dropped/invalid packets and command rejections for diagnostics rates.
- `server/src/main.cpp` now omits `match_state.scoreboard` from cadence `state_snapshot` payloads by default (`runtime.snapshot_include_match_scoreboard=false`) to reduce duplicated snapshot bytes while preserving full scoreboard in reliable `match_state` packets.
- `server/src/main.cpp` snapshot build path now uses session-indexed player view assembly (instead of multiple per-snapshot hash-map joins) and reserves JSON array capacities for known snapshot payload sizes.
- `tests/CMakeLists.txt` now includes `RuntimeTelemetryTests` in `server.unit`.
- `tests/CMakeLists.txt` now includes `MatchStatePayloadTests` in `server.unit`.
- `server/src/main.cpp` now fails fast during startup when config validation reports invalid
  fields, instead of silently clamping known invalid values.
- `shared/Config` now exposes `try_load_json(...)` and `server/src/main.cpp` now fails fast on
  config load/parse errors (missing file or invalid JSON) before runtime initialization.
- `tools/devy_load_client.cpp` now supports reliability chaos modes:
  - protocol-invalid packet flood emission (`--malformed-rate-hz`),
  - malformed family selection (`--malformed-family`) across envelope/schema/version/type/json error classes,
  - malformed burst envelopes (`--malformed-burst-size`) to stress parse/reject paths,
  - forced disconnect/reconnect churn (`--disconnect-interval-ms`, `--reconnect-delay-ms`),
  - expanded summary counters for connect/reconnect/disconnect/malformed packet assertions.
- `tests/CMakeLists.txt` now includes `server.reliability.*` integration tests for watchdog,
  chaos-drill, and restart-recovery resilience checks.
- `scripts/chaos-drill.sh` now accepts malformed family/burst parameters and verifies expected
  invalid-packet error-category coverage (including mixed-family breadth checks).
- `.github/workflows/reliability.yml` now sets explicit artifact retention windows:
  - reliability drill artifacts: 14 days,
  - soak artifacts: 30 days.
- `scripts/package-artifacts.sh` now bundles release-operation inputs (`config/templates/*.json`,
  `profiles/launch/*.env`, `docs/rollback-strategy.md`, and optional `docs/releases/*.md`) into
  packaged artifacts.
- `.github/workflows/ci.yml` integration shard regex now includes `server.release.*` checks, including alpha-gate dry checks (`server.release.alpha_acceptance_pack`, `server.release.alpha_endurance_short`).
- `scripts/generate-release-notes.sh` now correctly counts the final commit in range scans when
  git-log output has no trailing newline (fixes zero-commit output on single-commit ranges).
- `.github/workflows/ci.yml` integration shard regex now also includes
  `server.release.release_notes_generation`.
- `docs/releases/alpha-bug-bash-checklist.md` now records closure evidence and marks checklist/sign-off complete for the latest isolated-port acceptance/gate evidence set.
- `docs/releases/alpha-acceptance-checklist.md` now records live 8-hour endurance status snapshot details and marks release metadata checklist items complete with the latest generated alpha notes file.
- Added `scripts/alpha-post-endurance-followup.sh` to automate the final TODO path by waiting for
  the active endurance run to pass, then executing default-port acceptance+regression and alpha
  release-gate follow-up with baseline-tag release-note generation.
- Updated `README.md`, `AGENTS.md`, `docs/decision-log.md`, and `docs/project-status.md` with the
  queued post-endurance follow-up workflow and active worker evidence paths.
- Refreshed release-monitoring docs with a new endurance/follow-up status snapshot and helper-script
  validation evidence while final release tagging remains blocked on the in-flight 8-hour run.
- Recorded another live TODO monitoring checkpoint (`2026-02-15T18:52:26Z`) confirming endurance and
  queued follow-up workers are still running and final alpha release commit/tag remains gated.
- Added `scripts/alpha-finalize-release.sh` to automate the final release cut by waiting for
  post-endurance follow-up pass, generating `v0.2.0-alpha` notes, and creating final
  `chore(release)` commit/tag.
- Updated `README.md`, `AGENTS.md`, `docs/decision-log.md`, and `docs/project-status.md` with
  finalizer usage and detached worker monitoring paths.
- Refreshed release-monitoring docs with a live liveness checkpoint (`2026-02-15T18:57:23Z`)
  confirming endurance (`chaos_cycles_completed=153`, `restart_runs_completed=25`) and queued
  follow-up/finalizer workers are still running and waiting as designed.
- Refreshed release-monitoring docs with another live liveness checkpoint (`2026-02-15T18:58:54Z`)
  confirming endurance (`chaos_cycles_completed=156`, `restart_runs_completed=26`) and queued
  follow-up/finalizer workers are still running and waiting as designed.
- Refreshed release-monitoring docs with another live liveness checkpoint (`2026-02-15T19:00:07Z`)
  confirming endurance (`chaos_cycles_completed=159`, `restart_runs_completed=26`) and queued
  follow-up/finalizer workers are still running and waiting as designed.
- Refreshed release-monitoring docs with another live liveness checkpoint (`2026-02-15T19:01:29Z`)
  confirming endurance (`chaos_cycles_completed=162`, `restart_runs_completed=27`) and queued
  follow-up/finalizer workers are still running and waiting as designed.
- Refreshed release-monitoring docs with another live liveness checkpoint (`2026-02-15T19:03:17Z`)
  confirming endurance (`chaos_cycles_completed=166`, `restart_runs_completed=27`) and queued
  follow-up/finalizer workers are still running and waiting as designed.
- Refreshed release-monitoring docs with another live liveness checkpoint (`2026-02-15T19:04:15Z`)
  confirming endurance (`chaos_cycles_completed=168`, `restart_runs_completed=28`) and queued
  follow-up/finalizer workers are still running and waiting as designed.
- Refreshed release-monitoring docs with another live liveness checkpoint (`2026-02-15T19:05:25Z`)
  confirming endurance (`chaos_cycles_completed=171`, `restart_runs_completed=28`) and queued
  follow-up/finalizer workers are still running and waiting as designed.
- Refreshed release-monitoring docs with another live liveness checkpoint (`2026-02-15T19:06:35Z`)
  confirming endurance (`chaos_cycles_completed=174`, `restart_runs_completed=29`) and queued
  follow-up/finalizer workers are still running and waiting as designed.
- Refreshed release-monitoring docs with another live liveness checkpoint (`2026-02-15T19:07:47Z`)
  confirming endurance (`chaos_cycles_completed=177`, `restart_runs_completed=29`) and queued
  follow-up/finalizer workers are still running and waiting as designed.
- Refreshed release-monitoring docs with another live liveness checkpoint (`2026-02-15T19:08:39Z`)
  confirming endurance (`chaos_cycles_completed=179`, `restart_runs_completed=29`) and queued
  follow-up/finalizer workers are still running and waiting as designed.
- Refreshed release-monitoring docs with another live liveness checkpoint (`2026-02-15T19:10:34Z`)
  confirming endurance (`chaos_cycles_completed=183`, `restart_runs_completed=30`) and queued
  follow-up/finalizer workers are still running and waiting as designed.
- Refreshed release-monitoring docs with another live liveness checkpoint (`2026-02-15T19:12:26Z`)
  confirming endurance (`chaos_cycles_completed=187`, `restart_runs_completed=31`) and queued
  follow-up/finalizer workers are still running and waiting as designed.
- Refreshed release-monitoring docs with another live liveness checkpoint (`2026-02-15T19:13:30Z`)
  confirming endurance (`chaos_cycles_completed=190`, `restart_runs_completed=31`) and queued
  follow-up/finalizer workers are still running and waiting as designed.
- Refreshed release-monitoring docs with another live liveness checkpoint (`2026-02-15T19:14:26Z`)
  confirming endurance (`chaos_cycles_completed=192`, `restart_runs_completed=31`) and queued
  follow-up/finalizer workers are still running and waiting as designed.
- Refreshed release-monitoring docs with another live liveness checkpoint (`2026-02-15T19:15:24Z`)
  confirming endurance (`chaos_cycles_completed=194`, `restart_runs_completed=32`) and queued
  follow-up/finalizer workers are still running and waiting as designed.
- Refreshed release-monitoring docs with another live liveness checkpoint (`2026-02-15T19:16:19Z`)
  confirming endurance (`chaos_cycles_completed=196`, `restart_runs_completed=32`) and queued
  follow-up/finalizer workers are still running and waiting as designed.
- Refreshed release-monitoring docs with another live liveness checkpoint (`2026-02-15T19:17:17Z`)
  confirming endurance (`chaos_cycles_completed=198`, `restart_runs_completed=33`) and queued
  follow-up/finalizer workers are still running and waiting as designed.
- Refreshed release-monitoring docs with another live liveness checkpoint (`2026-02-15T19:18:10Z`)
  confirming endurance (`chaos_cycles_completed=200`, `restart_runs_completed=33`) and queued
  follow-up/finalizer workers are still running and waiting as designed.
- Refreshed release-monitoring docs with another live liveness checkpoint (`2026-02-15T19:19:09Z`)
  confirming endurance (`chaos_cycles_completed=203`, `restart_runs_completed=33`) and queued
  follow-up/finalizer workers are still running and waiting as designed.
