# Devy FPS Remaining TODO (Fast Path)

This file now contains only work that is still open.

## 1) Remaining Implementations (AI Expert)

- [ ] A1. Replace preview-only client world with authoritative replicated world consumption.
  - Scope: consume server `state_snapshot.payload.chunk_sync` deltas in interactive client and remove `map.preview_chunks` fallback generation.
  - Targets: `client/src/main.cpp` and client runtime chunk/mesh update path.
  - Done when: no preview fallback warning; cold-join + chunk delta application validated with test/doc note.

- [ ] A2. Promote client gameplay networking from scaffold to authoritative integration.
  - Scope: full join/heartbeat/input/fire/pickup intent flow + consume authoritative outputs (`damage_event`, `death_event`, `inventory_update`, `match_state`, ack/state fields).
  - Targets: `client/src/main.cpp`, client runtime protocol handling, `tests/client/*` (+ shared/server integration tests as needed).
  - Done when: interactive client session gameplay works against server without local-only fallback logic.

- [ ] A3. Close Phase 4.3 diagnostics deliverable with operator-facing health surface.
  - Scope: stable health interface (endpoint and/or machine-readable summary command) mapped to runtime telemetry/alerts.
  - Targets: server diagnostics surface + docs + automated validation.
  - Done when: operators can query health via stable interface and CI validates it.

## 2) Remaining Release Ops Before `main`

- [ ] D3. Prepare commit set for merge.
  - Use Conventional Commits.
  - Keep docs/evidence sync commit separate from code changes when practical.

- [ ] D4. Push branch and verify required checks are green.

## 3) Already Completed (Evidence)

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
