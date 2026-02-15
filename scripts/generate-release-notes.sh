#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

from_ref="${1:-}"
to_ref="${2:-HEAD}"
out_file="${3:-${repo_root}/docs/releases/release-notes-$(date -u +%Y%m%d-%H%M%S).md}"

if [[ -z "${from_ref}" ]]; then
  from_ref="$(git -C "${repo_root}" describe --tags --abbrev=0 2>/dev/null || true)"
fi

if ! git -C "${repo_root}" rev-parse --verify --quiet "${to_ref}" >/dev/null; then
  echo "Invalid to-ref: ${to_ref}" >&2
  exit 1
fi
if [[ -n "${from_ref}" ]] && ! git -C "${repo_root}" rev-parse --verify --quiet "${from_ref}" >/dev/null; then
  echo "Invalid from-ref: ${from_ref}" >&2
  exit 1
fi

tmp_dir="$(mktemp -d)"
trap 'rm -rf "${tmp_dir}"' EXIT

section_order=(feat fix perf refactor test docs build ci chore other)
for key in "${section_order[@]}"; do
  : >"${tmp_dir}/${key}.txt"
done

map_type_to_key() {
  local subject="$1"
  local token
  token="${subject%%:*}"
  if [[ "${token}" == "${subject}" ]]; then
    echo "other"
    return
  fi
  token="${token%%(*}"
  token="${token,,}"
  case "${token}" in
    feat|fix|perf|refactor|test|docs|build|ci|chore)
      echo "${token}"
      ;;
    *)
      echo "other"
      ;;
  esac
}

commit_count=0
if [[ -n "${from_ref}" ]]; then
  range_desc="${from_ref}..${to_ref}"
  log_cmd=(git -C "${repo_root}" log --reverse --pretty=format:'%h%x09%s' "${from_ref}..${to_ref}")
else
  range_desc="(all commits up to ${to_ref})"
  log_cmd=(git -C "${repo_root}" log --reverse --pretty=format:'%h%x09%s' "${to_ref}")
fi

while IFS=$'\t' read -r short_hash subject || [[ -n "${short_hash:-}" ]]; do
  if [[ -z "${short_hash}" ]]; then
    continue
  fi
  commit_count=$((commit_count + 1))
  key="$(map_type_to_key "${subject}")"
  printf -- "- %s (\`%s\`)\n" "${subject}" "${short_hash}" >>"${tmp_dir}/${key}.txt"
done < <("${log_cmd[@]}")

mkdir -p "$(dirname "${out_file}")"

{
  echo "# Release Notes"
  echo
  echo "Generated (UTC): $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "Range: ${range_desc}"
  echo "Total commits: ${commit_count}"
  echo

  if (( commit_count == 0 )); then
    echo "No commits found for this range."
    exit 0
  fi

  for key in "${section_order[@]}"; do
    if [[ ! -s "${tmp_dir}/${key}.txt" ]]; then
      continue
    fi

    case "${key}" in
      feat) title="Features" ;;
      fix) title="Fixes" ;;
      perf) title="Performance" ;;
      refactor) title="Refactors" ;;
      test) title="Tests" ;;
      docs) title="Documentation" ;;
      build) title="Build" ;;
      ci) title="CI" ;;
      chore) title="Chores" ;;
      other) title="Other" ;;
    esac

    echo "## ${title}"
    cat "${tmp_dir}/${key}.txt"
    echo
  done
} >"${out_file}"

echo "Release notes written to ${out_file}"
