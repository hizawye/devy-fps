# Devy FPS Remaining TODO (Fast Path)

This file now contains only work that is still open.

## 1) Remaining Implementations (AI Expert)

- None.

## 2) Remaining Release Ops Before `main`

- [ ] D3. Prepare commit set for merge.
  - Use Conventional Commits.
  - Keep docs/evidence sync commit separate from code changes when practical.

- [ ] D4. Push branch and verify required checks are green.

## 3) Already Completed (Evidence)

- A1 complete: interactive client now consumes authoritative `chunk_sync` and no longer uses preview fallback world generation.
  - `client/src/main.cpp`
  - `client/include/client/ChunkSyncApplier.h`
  - `client/src/ChunkSyncApplier.cpp`
  - `tests/client/ChunkSyncApplierTests.cpp`

- A2 complete: interactive client now sends authoritative join/heartbeat/input/fire/pickup intents and consumes authoritative gameplay outputs (`state_snapshot`, `damage_event`, `inventory_update`, `match_state`).
  - `client/src/main.cpp`

- A3 complete: server now supports stable operator-facing health snapshot output via `--health-file`, with CI-validated dry-run test.
  - `server/src/main.cpp`
  - `scripts/health-file-dry-run.sh`
  - `tests/scripts/assert-health-file-dry-run.sh`
  - `tests/CMakeLists.txt`
  - `.github/workflows/ci.yml`
  - `.github/workflows/reliability.yml`

- Fast gate pass (`status=pass`, `endurance_status=skipped`):
  - `artifacts/releases/alpha-gate/fast-mainline/summary.txt`
- Acceptance pack pass:
  - `artifacts/releases/alpha-acceptance/fast-mainline/summary.txt`
- Endurance short pass:
  - `artifacts/releases/ctest-alpha-endurance-short/summary.txt`
- Release notes file exists:
  - `docs/releases/v0.2.0-alpha-fast-notes.md`
- Local validation pass:
  - `scripts/configure.sh debug-vcpkg`
  - `scripts/build.sh debug-vcpkg`
  - `scripts/test.sh debug-vcpkg`
- Release-focused subset pass:
  - `DEVY_TEST_CONFIG_PATH=$(pwd)/artifacts/tmp/server_test_port18777.json ctest --preset debug-vcpkg -R '^(shared\.unit|client\.unit|server\.unit|server\.(smoke|config\.invalid_tick_rate|config\.invalid_loot_drop|config\.invalid_json|telemetry\.alert_dry_run|release\.install_smoke|release\.protocol_upgrade_downgrade|release\.rollback_rehearsal|release\.alpha_acceptance_pack|release\.alpha_endurance_short|release\.release_notes_generation))$' --output-on-failure`
