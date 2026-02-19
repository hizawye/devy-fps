# Beta Final Acceptance Checklist

Use this checklist for `v0.3.0-beta` candidate validation and tag readiness.

## Candidate Inputs
- Version tag candidate: `v0.3.0-beta` (or pre-tag candidate like `v0.3.0-beta-rc1`).
- Release-note range start (`from_ref`): previous published release tag, typically `v0.2.0-alpha`.
- Test config: `config/server_test.json` (or isolated override via `DEVY_TEST_CONFIG_PATH`).
- Artifact root (example): `artifacts/releases/beta-candidate/latest`.

## Core Build + Unit Gates
- Configure:
  - `scripts/configure.sh debug-vcpkg`
- Build:
  - `scripts/build.sh debug-vcpkg`
- Test:
  - `scripts/test.sh debug-vcpkg`
- Gate:
  - all commands exit `0`.

## Release-Lane Checks
- Command:
  - `ctest --preset debug-vcpkg -R '^server\.release\.' --output-on-failure`
- Gate:
  - targeted release-lane tests pass for the candidate commit.

## Multiplayer Acceptance Pack
- Command:
  - `scripts/alpha-acceptance-pack.sh config/server_test.json artifacts/releases/beta-acceptance/latest debug-vcpkg 8 8 1`
- Required scenario outcomes:
  - `profile_load_status=pass`,
  - `chaos_drill_status=pass`,
  - `restart_recovery_status=pass`.
- Evidence:
  - `artifacts/releases/beta-acceptance/latest/summary.txt`.

## Reliability Drill Evidence
- Reliability test subset:
  - `ctest --preset debug-vcpkg -R '^server\.reliability\.' --output-on-failure`
- Soak run:
  - `scripts/reliability-soak.sh config/server_test.json 30 8 artifacts/releases/beta-soak/latest 20 6 6`
- Required soak summary fields:
  - `schema_version=1`,
  - `summary_kind=reliability_soak`,
  - `status=pass`.
- Evidence:
  - `artifacts/releases/beta-soak/latest/summary.txt`.

## Beta Gate Dry-Run (No Endurance)
- Command:
  - `scripts/beta-release-gate.sh v0.3.0-beta-candidate config/server_test.json debug-vcpkg 8 8 30 artifacts/releases/beta-gate/dry-run 0 v0.2.0-alpha`
- Required summary fields:
  - `status=pass`,
  - `acceptance_status=pass`,
  - `endurance_status=skipped`,
  - `release_notes_status=pass`,
  - `missing_required_docs=0`.
- Evidence:
  - `artifacts/releases/beta-gate/dry-run/summary.txt`.

## Release Metadata Checklist
- [ ] Known issues updated: `docs/releases/beta-known-issues.md`.
- [ ] Tag flow validated: `docs/releases/beta-release-tag-flow.md`.
- [ ] Candidate notes generated and reviewed: `docs/releases/v0.3.0-beta-notes.md`.
- [ ] Notes header `Range:` matches intended `from_ref..HEAD`.

## Final Sign-Off
- [ ] Gameplay owner sign-off.
- [ ] Networking owner sign-off.
- [ ] Runtime ops owner sign-off.
- [ ] Release owner sign-off.
