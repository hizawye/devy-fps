#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

followup_out="${1:-${repo_root}/artifacts/releases/post-endurance/followup-20260215-193837}"
version_tag="${2:-v0.2.0-alpha}"
from_ref="${3:-v0.2.0-alpha-baseline}"
poll_seconds="${4:-60}"
out_dir="${5:-${repo_root}/artifacts/releases/post-endurance/finalize-$(date +%Y%m%d-%H%M%S)}"
create_tag="${6:-1}"

if [[ ! -d "${followup_out}" ]]; then
  echo "Follow-up output directory not found: ${followup_out}" >&2
  exit 1
fi

for pair in \
  "poll_seconds:${poll_seconds}" \
  "create_tag:${create_tag}"; do
  key="${pair%%:*}"
  value="${pair##*:}"
  if ! [[ "${value}" =~ ^[0-9]+$ ]]; then
    echo "${key} must be a non-negative integer" >&2
    exit 1
  fi
done
if (( poll_seconds <= 0 )) || (( create_tag > 1 )); then
  echo "Invalid numeric arguments" >&2
  exit 1
fi

mkdir -p "${out_dir}"
status_log="${out_dir}/status.log"
summary_log="${out_dir}/summary.txt"
release_notes_log="${out_dir}/release-notes.log"

followup_summary="${followup_out}/summary.txt"
followup_status="missing"

echo "Waiting for post-endurance follow-up summary: ${followup_summary}"
while true; do
  timestamp="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  if [[ ! -f "${followup_summary}" ]]; then
    {
      echo "[${timestamp}] waiting: summary not found"
      echo
    } >>"${status_log}"
    sleep "${poll_seconds}"
    continue
  fi

  followup_status="$(awk -F= '/^status=/{print $2}' "${followup_summary}" | tail -n1)"
  {
    echo "[${timestamp}] followup_status=${followup_status:-unknown}"
    tail -n 20 "${followup_summary}" 2>/dev/null || true
    echo
  } >>"${status_log}"

  if [[ "${followup_status}" == "pass" ]]; then
    break
  fi
  if [[ "${followup_status}" == "fail" ]]; then
    {
      echo "followup_out=${followup_out}"
      echo "followup_summary=${followup_summary}"
      echo "followup_status=${followup_status}"
      echo "status=fail"
    } >"${summary_log}"
    echo "Post-endurance follow-up failed; refusing release finalization." >&2
    exit 1
  fi

  sleep "${poll_seconds}"
done

acceptance_summary="$(awk -F= '/^acceptance_summary=/{print $2}' "${followup_summary}" | tail -n1)"
gate_summary="$(awk -F= '/^gate_summary=/{print $2}' "${followup_summary}" | tail -n1)"

if [[ -z "${acceptance_summary}" ]] || [[ -z "${gate_summary}" ]]; then
  {
    echo "followup_out=${followup_out}"
    echo "followup_summary=${followup_summary}"
    echo "error=missing acceptance_summary or gate_summary in follow-up summary"
    echo "status=fail"
  } >"${summary_log}"
  echo "Missing acceptance/gate summary paths in ${followup_summary}" >&2
  exit 1
fi

if [[ ! -f "${acceptance_summary}" ]] || [[ ! -f "${gate_summary}" ]]; then
  {
    echo "followup_out=${followup_out}"
    echo "followup_summary=${followup_summary}"
    echo "acceptance_summary=${acceptance_summary}"
    echo "gate_summary=${gate_summary}"
    echo "error=referenced acceptance/gate summary missing"
    echo "status=fail"
  } >"${summary_log}"
  echo "Acceptance/gate summary file missing." >&2
  exit 1
fi

acceptance_status="$(awk -F= '/^status=/{print $2}' "${acceptance_summary}" | tail -n1)"
gate_status="$(awk -F= '/^status=/{print $2}' "${gate_summary}" | tail -n1)"

if [[ "${acceptance_status}" != "pass" ]] || [[ "${gate_status}" != "pass" ]]; then
  {
    echo "followup_out=${followup_out}"
    echo "followup_summary=${followup_summary}"
    echo "acceptance_summary=${acceptance_summary}"
    echo "gate_summary=${gate_summary}"
    echo "acceptance_status=${acceptance_status:-unknown}"
    echo "gate_status=${gate_status:-unknown}"
    echo "error=acceptance or gate did not pass"
    echo "status=fail"
  } >"${summary_log}"
  echo "Acceptance/gate status is not pass." >&2
  exit 1
fi

cd "${repo_root}"
if ! git diff --quiet || ! git diff --cached --quiet || [[ -n "$(git ls-files --others --exclude-standard)" ]]; then
  {
    echo "followup_out=${followup_out}"
    echo "error=working tree is dirty; refusing automated release finalization"
    echo "status=fail"
  } >"${summary_log}"
  echo "Working tree is dirty; aborting automated release finalization." >&2
  exit 1
fi

if git rev-parse -q --verify "refs/tags/${version_tag}" >/dev/null; then
  {
    echo "followup_out=${followup_out}"
    echo "followup_summary=${followup_summary}"
    echo "acceptance_summary=${acceptance_summary}"
    echo "gate_summary=${gate_summary}"
    echo "version_tag=${version_tag}"
    echo "from_ref=${from_ref}"
    echo "status=pass"
    echo "note=tag already exists; finalization treated as complete"
  } >"${summary_log}"
  echo "Tag ${version_tag} already exists; nothing to do."
  exit 0
fi

release_notes_file="${repo_root}/docs/releases/${version_tag}-notes.md"
set +e
"${repo_root}/scripts/generate-release-notes.sh" "${from_ref}" HEAD "${release_notes_file}" >"${release_notes_log}" 2>&1
notes_exit=$?
set -e
if (( notes_exit != 0 )); then
  {
    echo "followup_out=${followup_out}"
    echo "followup_summary=${followup_summary}"
    echo "acceptance_summary=${acceptance_summary}"
    echo "gate_summary=${gate_summary}"
    echo "version_tag=${version_tag}"
    echo "from_ref=${from_ref}"
    echo "release_notes_file=${release_notes_file}"
    echo "release_notes_exit=${notes_exit}"
    echo "status=fail"
  } >"${summary_log}"
  echo "Release notes generation failed. See ${release_notes_log}" >&2
  exit 1
fi

git add "${release_notes_file}"
git commit --allow-empty -m "chore(release): ${version_tag}"

tag_created=0
if (( create_tag == 1 )); then
  git tag -a "${version_tag}" -m "chore(release): ${version_tag}"
  tag_created=1
fi

release_commit="$(git rev-parse --short HEAD)"
{
  echo "followup_out=${followup_out}"
  echo "followup_summary=${followup_summary}"
  echo "acceptance_summary=${acceptance_summary}"
  echo "gate_summary=${gate_summary}"
  echo "acceptance_status=${acceptance_status}"
  echo "gate_status=${gate_status}"
  echo "version_tag=${version_tag}"
  echo "from_ref=${from_ref}"
  echo "release_notes_file=${release_notes_file}"
  echo "release_notes_log=${release_notes_log}"
  echo "release_commit=${release_commit}"
  echo "tag_created=${tag_created}"
  echo "status=pass"
} >"${summary_log}"

echo "Release finalization passed. summary=${summary_log}"
