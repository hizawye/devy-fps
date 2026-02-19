# Release Notes Artifacts

Generated release notes can be written here with:

`scripts/generate-release-notes.sh [from-ref] [to-ref] [out-file]`

Example:

`scripts/generate-release-notes.sh v0.1.0 HEAD docs/releases/v0.2.0-alpha-notes.md`

Alpha release-gate docs:
- `docs/releases/alpha-bug-bash-checklist.md`
- `docs/releases/alpha-known-issues.md`
- `docs/releases/alpha-acceptance-checklist.md`
- `docs/releases/alpha-release-tag-flow.md`

Beta release-gate docs:
- `docs/releases/beta-known-issues.md`
- `docs/releases/beta-acceptance-checklist.md`
- `docs/releases/beta-release-tag-flow.md`

Post-alpha planning docs:
- `docs/releases/v0.3.0-beta-plan.md`
