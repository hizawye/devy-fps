# Beta Tag And Release Flow

Use this runbook for `v0.3.0-beta` tag cut.

## 1. Resolve Release-Notes Range
- Default `from_ref`:
  - `v0.2.0-alpha`
- Validate refs before running gates:
  - `git rev-parse --verify v0.2.0-alpha`
  - `git rev-parse --verify HEAD`
- If beta is a rerun after prior beta tags, set `from_ref` to the intended previous beta tag explicitly.

## 2. Candidate Gate Dry-Run (Skip Endurance)
- Command:
  - `scripts/beta-release-gate.sh v0.3.0-beta-candidate config/server_test.json debug-vcpkg 8 8 30 artifacts/releases/beta-gate/dry-run 0 v0.2.0-alpha`
- Gate:
  - `summary.txt` includes:
    - `status=pass`,
    - `acceptance_status=pass`,
    - `endurance_status=skipped`,
    - `release_notes_status=pass`,
    - `missing_required_docs=0`.

## 3. Final Gate Run (With Endurance)
- Command:
  - `scripts/beta-release-gate.sh v0.3.0-beta config/server_test.json debug-vcpkg 8 8 30 artifacts/releases/beta-gate/final 1 v0.2.0-alpha`
- Gate:
  - final `summary.txt` reports `status=pass` and `endurance_status=pass`.

## 4. Generate Final Release Notes (Explicit Range)
- Command:
  - `scripts/generate-release-notes.sh v0.2.0-alpha HEAD docs/releases/v0.3.0-beta-notes.md`
- Validation:
  - `docs/releases/v0.3.0-beta-notes.md` exists.
  - `Range:` header equals `v0.2.0-alpha..HEAD`.

## 5. Stage, Commit, Tag, Push
- Stage release docs and notes:
  - `git add docs/releases/v0.3.0-beta-notes.md docs/releases/beta-*.md docs/releases/README.md`
- Commit:
  - `git commit -m "chore(release): v0.3.0-beta"`
- Annotated tag:
  - `git tag -a v0.3.0-beta -m "chore(release): v0.3.0-beta"`
- Push branch and tags:
  - `git push origin HEAD --tags`

## 6. Post-Tag
- [ ] Attach gate artifacts and summaries to release draft.
- [ ] Link `docs/releases/beta-known-issues.md` in release announcement.
- [ ] Open post-beta follow-up tracking issue for GA readiness.
