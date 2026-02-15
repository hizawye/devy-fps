# Alpha Tag And Release Flow

## Candidate Gate Run
- Full gate command:
  - `scripts/alpha-release-gate.sh v0.x.y-alpha config/server_test.json debug-vcpkg 8 8 480 artifacts/releases/alpha-gate/latest 1`
- Dry-run gate (skip endurance):
  - `scripts/alpha-release-gate.sh v0.x.y-alpha config/server_test.json debug-vcpkg 8 8 1 artifacts/releases/alpha-gate/dry-run 0`

## Release Notes
- Generate notes between previous tag and HEAD:
  - `scripts/generate-release-notes.sh <previous-tag> HEAD docs/releases/v0.x.y-alpha-notes.md`

## Tag Cut
- Stage release notes and gate docs updates:
  - `git add docs/releases/v0.x.y-alpha-notes.md docs/releases/alpha-*.md`
- Commit:
  - `git commit -m "chore(release): v0.x.y-alpha"`
- Annotated tag:
  - `git tag -a v0.x.y-alpha -m "chore(release): v0.x.y-alpha"`
- Push branch and tags:
  - `git push origin HEAD --tags`

## Post-Tag
- [ ] Attach release artifacts and gate summaries to release draft.
- [ ] Link known-issues register in release announcement.
- [ ] Start first external tester cycle and intake loop.
