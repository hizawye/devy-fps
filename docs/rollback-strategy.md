# Rollback Strategy

## Scope
- Runtime rollback for server/client artifacts produced by `scripts/package-artifacts.sh`.
- Assumes protocol major/minor compatibility remains fixed at v1 during alpha (`kMinSupportedProtocolVersion == kMaxSupportedProtocolVersion == 1`).

## Release Inputs
- Baseline artifact: currently deployed package + checksum.
- Candidate artifact: incoming release package + checksum.
- Config profile: selected from `config/templates/*.json` and tracked with the deployment change.
- Launch profile: selected from `profiles/launch/*.env` for operator runbooks.

## Pre-Deploy Checklist
1. Validate deterministic package hash in CI (`scripts/verify-artifact-repro.sh`).
2. Generate release notes (`scripts/generate-release-notes.sh`) and attach to deploy ticket.
3. Execute fresh-install rehearsal (`scripts/install-package-smoke.sh`).
4. Execute protocol compatibility rehearsal (`scripts/protocol-compat-check.sh`).
5. Keep previous known-good package and checksum staged on disk for fast rollback.

## Rollback Triggers
- Crash loop in the first deployment window.
- Protocol parse/drop surge above alert thresholds.
- Join success regression during canary or full rollout.
- Match lifecycle regressions (no transitions, respawn failures, scoreboard corruption).

## Rollback Procedure
1. Stop candidate server process.
2. Re-point service symlink/process manager to baseline package.
3. Start baseline server with the prior config profile.
4. Run quick admission check using bundled `devy_load_client` (or `scripts/restart-recovery.sh` against baseline binary).
5. Confirm telemetry window recovery (tick lag, parse/drop rates, active player count).
6. Publish rollback notice with impacted release id and issue summary.

## Rehearsal Script
- Use `scripts/rollback-rehearsal.sh` to validate candidate deploy + baseline rollback using packaged artifacts.
- Pass criteria:
  - candidate run joins clients successfully,
  - rollback run joins clients successfully,
  - both runs exit cleanly under smoke timeout.

## Post-Rollback Follow-Up
1. Freeze further rollout.
2. Capture server/load logs and release notes diff.
3. File fix-forward ticket with repro steps and failing signal metrics.
4. Add regression test coverage before next release attempt.
