#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

endurance_out="${1:-${repo_root}/artifacts/releases/alpha-endurance/candidate-8h-20260215-184958}"
version_tag="${2:-v0.2.0-alpha-candidate}"
config_path="${3:-${repo_root}/config/server_test.json}"
preset="${4:-debug-vcpkg}"
clients="${5:-8}"
scenario_seconds="${6:-8}"
gate_endurance_minutes="${7:-480}"
from_ref="${8:-v0.2.0-alpha-baseline}"
poll_seconds="${9:-60}"
out_dir="${10:-${repo_root}/artifacts/releases/post-endurance/$(date +%Y%m%d-%H%M%S)}"
run_gate_endurance="${11:-0}"

if [[ ! -d "${endurance_out}" ]]; then
  echo "Endurance output directory not found: ${endurance_out}" >&2
  exit 1
fi

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
  "gate_endurance_minutes:${gate_endurance_minutes}" \
  "poll_seconds:${poll_seconds}" \
  "run_gate_endurance:${run_gate_endurance}"; do
  key="${pair%%:*}"
  value="${pair##*:}"
  if ! [[ "${value}" =~ ^[0-9]+$ ]]; then
    echo "${key} must be a non-negative integer" >&2
    exit 1
  fi
done
if (( clients <= 0 )) || (( scenario_seconds <= 0 )) || (( gate_endurance_minutes <= 0 )) || (( poll_seconds <= 0 )) || (( run_gate_endurance > 1 )); then
  echo "Invalid numeric arguments" >&2
  exit 1
fi

mkdir -p "${out_dir}"
status_log="${out_dir}/status.log"
acceptance_log="${out_dir}/acceptance.log"
gate_log="${out_dir}/gate.log"
summary_log="${out_dir}/summary.txt"
acceptance_out="${out_dir}/acceptance"
gate_out="${out_dir}/gate"

echo "Waiting for endurance completion: ${endurance_out}"
while true; do
  set +e
  status_output="$("${repo_root}/scripts/alpha-endurance-status.sh" "${endurance_out}" 2>&1)"
  status_exit=$?
  set -e

  timestamp="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  {
    echo "[${timestamp}] alpha-endurance-status exit=${status_exit}"
    echo "${status_output}"
    echo
  } >>"${status_log}"

  status_value="$(printf '%s\n' "${status_output}" | awk -F= '/^status=/{print $2}' | tail -n1)"
  if [[ "${status_value}" == "running" ]]; then
    sleep "${poll_seconds}"
    continue
  fi
  if [[ "${status_value}" != "pass" ]]; then
    {
      echo "endurance_out=${endurance_out}"
      echo "endurance_status=${status_value:-unknown}"
      echo "status=fail"
    } >"${summary_log}"
    echo "Endurance run did not pass; see ${status_log}" >&2
    exit 1
  fi
  break
done

echo "Endurance passed. Running default-port acceptance pack."
"${repo_root}/scripts/alpha-acceptance-pack.sh" \
  "${config_path}" \
  "${acceptance_out}" \
  "${preset}" \
  "${clients}" \
  "${scenario_seconds}" \
  1 >"${acceptance_log}" 2>&1

echo "Running alpha release gate."
"${repo_root}/scripts/alpha-release-gate.sh" \
  "${version_tag}" \
  "${config_path}" \
  "${preset}" \
  "${clients}" \
  "${scenario_seconds}" \
  "${gate_endurance_minutes}" \
  "${gate_out}" \
  "${run_gate_endurance}" \
  "${from_ref}" >"${gate_log}" 2>&1

acceptance_status="missing"
if [[ -f "${acceptance_out}/summary.txt" ]]; then
  acceptance_status="$(awk -F= '/^status=/{print $2}' "${acceptance_out}/summary.txt" | tail -n1)"
fi

gate_status="missing"
if [[ -f "${gate_out}/summary.txt" ]]; then
  gate_status="$(awk -F= '/^status=/{print $2}' "${gate_out}/summary.txt" | tail -n1)"
fi

overall="pass"
if [[ "${acceptance_status}" != "pass" ]] || [[ "${gate_status}" != "pass" ]]; then
  overall="fail"
fi

{
  echo "endurance_out=${endurance_out}"
  echo "config=${config_path}"
  echo "preset=${preset}"
  echo "clients=${clients}"
  echo "scenario_seconds=${scenario_seconds}"
  echo "version_tag=${version_tag}"
  echo "from_ref=${from_ref}"
  echo "run_gate_endurance=${run_gate_endurance}"
  echo "acceptance_summary=${acceptance_out}/summary.txt"
  echo "gate_summary=${gate_out}/summary.txt"
  echo "status=${overall}"
} >"${summary_log}"

if [[ "${overall}" == "pass" ]]; then
  echo "Post-endurance follow-up passed. Artifacts: ${out_dir}"
  exit 0
fi

echo "Post-endurance follow-up failed. See ${summary_log}" >&2
exit 1
