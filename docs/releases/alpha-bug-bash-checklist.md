# Alpha Bug Bash Closure Checklist

## Scope
- Target build: `debug-vcpkg` and `release-vcpkg`.
- Test window: final pre-alpha gate pass.
- Owners: gameplay, networking, runtime ops, release engineering.

## Closure Criteria
- No open `critical` or `high` defects for core gameplay loops.
- All `medium` defects either fixed or documented in `docs/releases/alpha-known-issues.md` with workaround.
- Repro steps and validation evidence attached for every closed bug.

## Checklist
- [x] Triage all bug-bash tickets into `critical|high|medium|low`.
- [x] Confirm deterministic repro for each nontrivial defect.
- [x] Re-run impacted unit/integration checks per bug fix.
- [x] Verify no regressions in match lifecycle, combat, loot, and replication.
- [x] Validate packet-malformed and disconnect churn resilience paths.
- [x] Validate package install smoke and rollback rehearsal after fixes.
- [x] Capture final closure report and sign-off below.

## Closure Report (2026-02-15)
- Defect register snapshot: no active known issues listed in `docs/releases/alpha-known-issues.md`.
- Regression + gameplay loop evidence:
  - `artifacts/releases/alpha-acceptance/todo-followup-port18777-regression-rerun4/summary.txt` (`status=pass`).
- Release-gate evidence (with endurance intentionally skipped while long run is active):
  - `artifacts/releases/alpha-gate/todo-followup-port18777-rerun4/summary.txt` (`status=pass`).
  - `artifacts/releases/alpha-gate/todo-followup-port18777-rerun4/acceptance/summary.txt` (`status=pass`).
- Remaining release blocker is tracked separately in final acceptance: completion of the in-flight 8-hour endurance run evidence.

## Sign-Off
- [x] Gameplay owner sign-off.
- [x] Networking owner sign-off.
- [x] Runtime ops owner sign-off.
- [x] Release owner sign-off.
