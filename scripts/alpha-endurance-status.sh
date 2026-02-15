#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
base_dir="${repo_root}/artifacts/releases/alpha-endurance"
out_dir="${1:-}"

if [[ -z "${out_dir}" ]]; then
  if [[ ! -d "${base_dir}" ]]; then
    echo "No endurance artifacts directory found: ${base_dir}" >&2
    exit 2
  fi
  out_dir="$(find "${base_dir}" -mindepth 1 -maxdepth 1 -type d -printf '%T@ %p\n' \
    | sort -nr \
    | head -n1 \
    | cut -d' ' -f2-)"
fi

if [[ ! -d "${out_dir}" ]]; then
  echo "Endurance output directory not found: ${out_dir}" >&2
  exit 2
fi

pid_file="${out_dir}/pid.txt"
run_summary_file="${out_dir}/summary.txt"
soak_summary_file="${out_dir}/soak/summary.txt"
chaos_csv="${out_dir}/soak/chaos-metrics.csv"
restart_csv="${out_dir}/soak/restart-metrics.csv"
timeline_log="${out_dir}/soak/timeline.log"

pid=""
process_state="missing"
if [[ -f "${pid_file}" ]]; then
  pid="$(tr -dc '0-9' <"${pid_file}")"
  if [[ -n "${pid}" ]] && kill -0 "${pid}" 2>/dev/null; then
    process_state="running"
  else
    process_state="exited"
  fi
fi

status="running"
if [[ -f "${run_summary_file}" ]]; then
  if grep -q '^status=pass$' "${run_summary_file}"; then
    status="pass"
  else
    status="fail"
  fi
elif [[ "${process_state}" != "running" ]]; then
  status="incomplete"
fi

chaos_cycles_completed=0
if [[ -f "${chaos_csv}" ]]; then
  chaos_cycles_completed="$(awk 'END { print (NR > 0 ? NR - 1 : 0) }' "${chaos_csv}")"
fi

restart_runs_completed=0
if [[ -f "${restart_csv}" ]]; then
  restart_runs_completed="$(awk 'END { print (NR > 0 ? NR - 1 : 0) }' "${restart_csv}")"
fi

echo "out_dir=${out_dir}"
echo "pid=${pid:-unknown}"
echo "process_state=${process_state}"
echo "chaos_cycles_completed=${chaos_cycles_completed}"
echo "restart_runs_completed=${restart_runs_completed}"
if [[ -f "${run_summary_file}" ]]; then
  echo "run_summary=${run_summary_file}"
else
  echo "run_summary=missing"
fi
if [[ -f "${soak_summary_file}" ]]; then
  echo "soak_summary=${soak_summary_file}"
else
  echo "soak_summary=missing"
fi
echo "status=${status}"

if [[ -f "${timeline_log}" ]]; then
  echo
  echo "recent_timeline:"
  tail -n 8 "${timeline_log}"
fi

if [[ "${status}" == "pass" ]] || [[ "${status}" == "running" ]]; then
  exit 0
fi

exit 1
