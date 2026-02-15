#!/usr/bin/env bash
set -euo pipefail

if [[ "$#" -lt 1 ]]; then
  echo "usage: $0 <repo-root>" >&2
  exit 2
fi

repo_root="$1"
out_dir="${repo_root}/artifacts/releases/ctest-release-notes"
notes_file="${out_dir}/notes.md"
rm -rf "${out_dir}"
mkdir -p "${out_dir}"

"${repo_root}/scripts/generate-release-notes.sh" "" HEAD "${notes_file}" >/dev/null

if [[ ! -f "${notes_file}" ]]; then
  echo "Missing generated release notes: ${notes_file}" >&2
  exit 1
fi

expected_commits="$(git -C "${repo_root}" rev-list --count HEAD)"
actual_commits="$(awk -F': ' '/^Total commits: / { print $2 }' "${notes_file}")"

if [[ -z "${actual_commits}" ]]; then
  echo "Release notes missing commit count line" >&2
  cat "${notes_file}"
  exit 1
fi

if [[ "${actual_commits}" != "${expected_commits}" ]]; then
  echo "Release notes commit count mismatch (expected=${expected_commits}, actual=${actual_commits})" >&2
  cat "${notes_file}"
  exit 1
fi
