#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

config_path="${1:-${repo_root}/config/server_test.json}"
duration_minutes="${2:-480}"
clients="${3:-8}"
out_dir="${4:-${repo_root}/artifacts/releases/alpha-endurance/$(date +%Y%m%d-%H%M%S)}"
chaos_seconds="${5:-20}"
restart_phase_seconds="${6:-6}"
restart_every_cycles="${7:-6}"

if [[ ! -f "${config_path}" && -f "${repo_root}/${config_path}" ]]; then
  config_path="${repo_root}/${config_path}"
fi
if [[ ! -f "${config_path}" ]]; then
  echo "Config file not found: ${config_path}" >&2
  exit 1
fi

for pair in \
  "duration_minutes:${duration_minutes}" \
  "clients:${clients}" \
  "chaos_seconds:${chaos_seconds}" \
  "restart_phase_seconds:${restart_phase_seconds}" \
  "restart_every_cycles:${restart_every_cycles}"; do
  key="${pair%%:*}"
  value="${pair##*:}"
  if ! [[ "${value}" =~ ^[0-9]+$ ]] || (( value <= 0 )); then
    echo "${key} must be a positive integer" >&2
    exit 1
  fi
done

mkdir -p "${out_dir}"
run_log="${out_dir}/run.log"
summary_log="${out_dir}/summary.txt"
soak_out="${out_dir}/soak"

set +e
"${repo_root}/scripts/reliability-soak.sh" \
  "${config_path}" \
  "${duration_minutes}" \
  "${clients}" \
  "${soak_out}" \
  "${chaos_seconds}" \
  "${restart_phase_seconds}" \
  "${restart_every_cycles}" >"${run_log}" 2>&1
soak_exit=$?
set -e

soak_summary="${soak_out}/summary.txt"
soak_status="fail"
if (( soak_exit == 0 )) && [[ -f "${soak_summary}" ]] && grep -q '^status=pass$' "${soak_summary}"; then
  soak_status="pass"
fi

{
  echo "config=${config_path}"
  echo "duration_minutes=${duration_minutes}"
  echo "clients=${clients}"
  echo "chaos_seconds=${chaos_seconds}"
  echo "restart_phase_seconds=${restart_phase_seconds}"
  echo "restart_every_cycles=${restart_every_cycles}"
  echo "soak_summary=${soak_summary}"
  echo "soak_exit_code=${soak_exit}"
  echo "status=${soak_status}"
} >"${summary_log}"

if [[ "${soak_status}" == "pass" ]]; then
  echo "Alpha endurance run passed. Artifacts: ${out_dir}"
  exit 0
fi

echo "Alpha endurance run failed. See ${summary_log}" >&2
exit 1
