# Alpha Final Acceptance Checklist

## Regression Suite
- Command:
  - `ctest --preset debug-vcpkg -R '^(shared\.unit|client\.unit|server\.unit|server\.(smoke|config\.invalid_tick_rate|config\.invalid_loot_drop|config\.invalid_json|telemetry\.alert_dry_run|release\.install_smoke|release\.protocol_upgrade_downgrade|release\.rollback_rehearsal))$' --output-on-failure`
- Evidence:
  - attach console output or `Testing/Temporary/LastTest.log` excerpt.
- Gate:
  - all tests pass.

## Multiplayer Acceptance Scenario Pack
- Command:
  - `scripts/alpha-acceptance-pack.sh config/server_test.json artifacts/releases/alpha-acceptance/latest debug-vcpkg 8 8 1`
- Required scenario outcomes:
  - profile-load pass,
  - chaos-drill pass,
  - restart-recovery pass.
- Evidence:
  - `artifacts/releases/alpha-acceptance/latest/summary.txt`

## Endurance Run Evidence (8h Target)
- Command:
  - `scripts/alpha-endurance-run.sh config/server_test.json 480 8 artifacts/releases/alpha-endurance/latest 20 6 6`
- Gate:
  - summary status pass with no watchdog/chaos/restart hard failures.
- Evidence:
  - `artifacts/releases/alpha-endurance/latest/summary.txt`
  - `artifacts/releases/alpha-endurance/latest/soak/summary.txt`
  - In-progress candidate run:
    - `artifacts/releases/alpha-endurance/candidate-8h-20260215-184958`
    - status snapshot (`2026-02-15T18:34:40Z`): `process_state=running`, `chaos_cycles_completed=102`, `restart_runs_completed=16`.

## Release Metadata
- [x] Bug bash closure checklist complete: `docs/releases/alpha-bug-bash-checklist.md`
- [x] Known issues register updated: `docs/releases/alpha-known-issues.md`
- [x] Release notes generated: `docs/releases/v0.2.0-alpha-todo-followup-rerun4-notes.md`
- [x] Tag flow prepared: `docs/releases/alpha-release-tag-flow.md`
