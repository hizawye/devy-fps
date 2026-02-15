#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

config_path="${1:-${repo_root}/config/server_test.json}"
out_dir="${2:-${repo_root}/artifacts/releases/alpha-acceptance/$(date +%Y%m%d-%H%M%S)}"
preset="${3:-debug-vcpkg}"
clients="${4:-8}"
scenario_seconds="${5:-8}"
run_regression="${6:-1}"

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
  "run_regression:${run_regression}"; do
  key="${pair%%:*}"
  value="${pair##*:}"
  if ! [[ "${value}" =~ ^[0-9]+$ ]]; then
    echo "${key} must be a non-negative integer" >&2
    exit 1
  fi
done
if (( clients <= 0 )) || (( scenario_seconds <= 0 )) || (( run_regression > 1 )); then
  echo "Invalid numeric arguments" >&2
  exit 1
fi

mkdir -p "${out_dir}"
regression_log="${out_dir}/regression.log"
scenario_log="${out_dir}/scenario.log"
summary_log="${out_dir}/summary.txt"

regression_status="skipped"
if (( run_regression == 1 )); then
  if [[ "${DEVY_SKIP_BUILD:-0}" != "1" ]]; then
    "${repo_root}/scripts/configure.sh" "${preset}" >"${out_dir}/configure.log" 2>&1
    "${repo_root}/scripts/build.sh" "${preset}" >"${out_dir}/build.log" 2>&1
  fi

  set +e
  DEVY_TEST_CONFIG_PATH="${config_path}" ctest --preset "${preset}" \
    -R '^(shared\.unit|client\.unit|server\.unit|server\.(smoke|config\.invalid_tick_rate|config\.invalid_loot_drop|config\.invalid_json|telemetry\.alert_dry_run|release\.install_smoke|release\.protocol_upgrade_downgrade|release\.rollback_rehearsal))$' \
    --output-on-failure >"${regression_log}" 2>&1
  regression_exit=$?
  set -e
  regression_status="fail"
  if (( regression_exit == 0 )); then
    regression_status="pass"
  fi
fi

profile_out="${out_dir}/scenarios/profile-load"
chaos_out="${out_dir}/scenarios/chaos-drill"
restart_out="${out_dir}/scenarios/restart-recovery"
mkdir -p "${out_dir}/scenarios"

set +e
(
  cd "${repo_root}"
  ./scripts/profile-load.sh \
    "${config_path}" \
    "${clients}" \
    "${scenario_seconds}" \
    "${profile_out}"
) >"${scenario_log}" 2>&1
profile_status=$?

"${repo_root}/scripts/chaos-drill.sh" \
  "${config_path}" \
  "${clients}" \
  "${scenario_seconds}" \
  "${chaos_out}" \
  20 \
  800 \
  mixed \
  3 >>"${scenario_log}" 2>&1
chaos_status=$?

restart_phase_seconds="4"
if (( scenario_seconds >= 10 )); then
  restart_phase_seconds="$((scenario_seconds / 2))"
fi

"${repo_root}/scripts/restart-recovery.sh" \
  "${config_path}" \
  "${clients}" \
  "${restart_phase_seconds}" \
  "${restart_out}" >>"${scenario_log}" 2>&1
restart_status=$?
set -e

overall="pass"
if [[ "${regression_status}" == "fail" ]] || (( profile_status != 0 )) || (( chaos_status != 0 )) || (( restart_status != 0 )); then
  overall="fail"
fi

{
  echo "config=${config_path}"
  echo "preset=${preset}"
  echo "clients=${clients}"
  echo "scenario_seconds=${scenario_seconds}"
  echo "run_regression=${run_regression}"
  echo "regression_status=${regression_status}"
  echo "profile_load_status=$([[ ${profile_status} -eq 0 ]] && echo pass || echo fail)"
  echo "chaos_drill_status=$([[ ${chaos_status} -eq 0 ]] && echo pass || echo fail)"
  echo "restart_recovery_status=$([[ ${restart_status} -eq 0 ]] && echo pass || echo fail)"
  echo "artifacts_dir=${out_dir}"
  echo "status=${overall}"
} >"${summary_log}"

if [[ "${overall}" == "pass" ]]; then
  echo "Alpha acceptance pack passed. Artifacts: ${out_dir}"
  exit 0
fi

echo "Alpha acceptance pack failed. See ${summary_log}" >&2
exit 1
