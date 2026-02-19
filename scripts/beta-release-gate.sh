#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

version_tag="${1:-v0.3.0-beta}"
config_path="${2:-${repo_root}/config/server_test.json}"
preset="${3:-debug-vcpkg}"
clients="${4:-8}"
scenario_seconds="${5:-8}"
endurance_minutes="${6:-30}"
out_dir="${7:-${repo_root}/artifacts/releases/beta-gate/$(date +%Y%m%d-%H%M%S)}"
run_endurance="${8:-1}"
from_ref="${9:-}"

if [[ ! -f "${config_path}" && -f "${repo_root}/${config_path}" ]]; then
  config_path="${repo_root}/${config_path}"
fi
if [[ ! -f "${config_path}" ]]; then
  echo "Config file not found: ${config_path}" >&2
  exit 1
fi
config_path="$(readlink -f "${config_path}")"

for pair in \
  "clients:${clients}" \
  "scenario_seconds:${scenario_seconds}" \
  "endurance_minutes:${endurance_minutes}" \
  "run_endurance:${run_endurance}"; do
  key="${pair%%:*}"
  value="${pair##*:}"
  if ! [[ "${value}" =~ ^[0-9]+$ ]]; then
    echo "${key} must be a non-negative integer" >&2
    exit 1
  fi
done
if (( clients <= 0 )) || (( scenario_seconds <= 0 )) || (( endurance_minutes <= 0 )) || (( run_endurance > 1 )); then
  echo "Invalid numeric arguments" >&2
  exit 1
fi

mkdir -p "${out_dir}"
summary_log="${out_dir}/summary.txt"
acceptance_out="${out_dir}/acceptance"
endurance_out="${out_dir}/endurance"
release_notes_log="${out_dir}/release-notes.log"

docs_required=(
  "${repo_root}/docs/releases/v0.3.0-beta-plan.md"
  "${repo_root}/docs/releases/README.md"
  "${repo_root}/docs/releases/beta-known-issues.md"
  "${repo_root}/docs/releases/beta-acceptance-checklist.md"
  "${repo_root}/docs/releases/beta-release-tag-flow.md"
)

set +e
(
  cd "${repo_root}"
  "${repo_root}/scripts/alpha-acceptance-pack.sh" \
    "${config_path}" \
    "${acceptance_out}" \
    "${preset}" \
    "${clients}" \
    "${scenario_seconds}" \
    1
)
acceptance_exit=$?
set -e

endurance_exit=0
if (( run_endurance == 1 )); then
  set +e
  (
    cd "${repo_root}"
    "${repo_root}/scripts/alpha-endurance-run.sh" \
      "${config_path}" \
      "${endurance_minutes}" \
      "${clients}" \
      "${endurance_out}" \
      20 \
      6 \
      6
  )
  endurance_exit=$?
  set -e
fi

notes_file="${repo_root}/docs/releases/${version_tag}-notes.md"
set +e
"${repo_root}/scripts/generate-release-notes.sh" "${from_ref}" HEAD "${notes_file}" >"${release_notes_log}" 2>&1
notes_exit=$?
set -e

missing_docs=0
for required in "${docs_required[@]}"; do
  if [[ ! -f "${required}" ]]; then
    missing_docs=$((missing_docs + 1))
  fi
done

overall="pass"
if (( acceptance_exit != 0 )) || (( notes_exit != 0 )) || (( missing_docs > 0 )); then
  overall="fail"
fi
if (( run_endurance == 1 )) && (( endurance_exit != 0 )); then
  overall="fail"
fi

{
  echo "version_tag=${version_tag}"
  echo "config=${config_path}"
  echo "preset=${preset}"
  echo "clients=${clients}"
  echo "scenario_seconds=${scenario_seconds}"
  echo "endurance_minutes=${endurance_minutes}"
  echo "run_endurance=${run_endurance}"
  echo "acceptance_status=$([[ ${acceptance_exit} -eq 0 ]] && echo pass || echo fail)"
  echo "acceptance_artifacts=${acceptance_out}"
  echo "endurance_status=$([[ ${run_endurance} -eq 0 ]] && echo skipped || ([[ ${endurance_exit} -eq 0 ]] && echo pass || echo fail))"
  echo "endurance_artifacts=${endurance_out}"
  echo "release_notes_status=$([[ ${notes_exit} -eq 0 ]] && echo pass || echo fail)"
  echo "release_notes_file=${notes_file}"
  echo "missing_required_docs=${missing_docs}"
  echo "status=${overall}"
  echo
  echo "tag_candidate_commands:"
  echo "  git add docs/releases/${version_tag}-notes.md"
  echo "  git commit -m \"chore(release): ${version_tag}\""
  echo "  git tag -a ${version_tag} -m \"chore(release): ${version_tag}\""
  echo "  git push origin HEAD --tags"
} >"${summary_log}"

if [[ "${overall}" == "pass" ]]; then
  echo "Beta release gate passed. Artifacts: ${out_dir}"
  exit 0
fi

echo "Beta release gate failed. See ${summary_log}" >&2
exit 1
