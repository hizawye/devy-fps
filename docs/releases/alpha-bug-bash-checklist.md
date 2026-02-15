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
- [ ] Triage all bug-bash tickets into `critical|high|medium|low`.
- [ ] Confirm deterministic repro for each nontrivial defect.
- [ ] Re-run impacted unit/integration checks per bug fix.
- [ ] Verify no regressions in match lifecycle, combat, loot, and replication.
- [ ] Validate packet-malformed and disconnect churn resilience paths.
- [ ] Validate package install smoke and rollback rehearsal after fixes.
- [ ] Capture final closure report and sign-off below.

## Sign-Off
- [ ] Gameplay owner sign-off.
- [ ] Networking owner sign-off.
- [ ] Runtime ops owner sign-off.
- [ ] Release owner sign-off.
