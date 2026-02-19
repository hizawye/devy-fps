#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

config_path="${1:-${repo_root}/config/server_test.json}"
run_seconds="${2:-4}"
max_restarts="${3:-3}"
backoff_seconds="${4:-1}"
out_dir="${5:-${repo_root}/artifacts/reliability/watchdog/$(date +%Y%m%d-%H%M%S)}"
rotation_keep="${6:-5}"

if [[ ! -f "${config_path}" && -f "${repo_root}/${config_path}" ]]; then
  config_path="${repo_root}/${config_path}"
fi

if [[ ! -f "${config_path}" ]]; then
  echo "Config file not found: ${config_path}" >&2
  exit 1
fi

if ! [[ "${run_seconds}" =~ ^[0-9]+$ ]] || (( run_seconds <= 0 )); then
  echo "run_seconds must be a positive integer" >&2
  exit 1
fi

if ! [[ "${max_restarts}" =~ ^[0-9]+$ ]]; then
  echo "max_restarts must be a non-negative integer" >&2
  exit 1
fi

if ! [[ "${rotation_keep}" =~ ^[0-9]+$ ]] || (( rotation_keep <= 0 )); then
  echo "rotation_keep must be a positive integer" >&2
  exit 1
fi

utc_now() { date -u +"%Y-%m-%dT%H:%M:%SZ"; }

find_server_bin() {
  if [[ -n "${DEVY_SERVER_BIN:-}" ]]; then
    echo "${DEVY_SERVER_BIN}"
    return
  fi

  local candidates=(
    "${repo_root}/build/presets/debug/server/devy_server"
    "${repo_root}/build/presets/debug-vcpkg/server/devy_server"
    "${repo_root}/build/presets/release/server/devy_server"
    "${repo_root}/build/presets/release-vcpkg/server/devy_server"
    "${repo_root}/build/debug/server/devy_server"
    "${repo_root}/build/debug/devy_server"
    "${repo_root}/build/server/devy_server"
    "${repo_root}/build/devy_server"
  )

  local candidate
  for candidate in "${candidates[@]}"; do
    if [[ -x "${candidate}" ]]; then
      echo "${candidate}"
      return
    fi
  done

  echo ""
}

server_bin="$(find_server_bin)"
if [[ -z "${server_bin}" ]]; then
  echo "Could not find devy_server binary. Set DEVY_SERVER_BIN or run scripts/build.sh first." >&2
  exit 1
fi

mkdir -p "${out_dir}"
summary_file="${out_dir}/summary.txt"
attempt_metrics_csv="${out_dir}/attempt-metrics.csv"
echo "attempt,exit_code,runtime_seconds,log_file,injected_kill" >"${attempt_metrics_csv}"

attempt=0
restart_count=0
success=0
last_exit=0
kill_after_first_seconds="${DEVY_WATCHDOG_KILL_FIRST_AFTER_SECONDS:-0}"
run_start_epoch="$(date +%s)"
run_started_at_utc="$(utc_now)"

while (( attempt <= max_restarts )); do
  attempt=$((attempt + 1))
  log_file="${out_dir}/server-attempt-${attempt}.log"

  start_epoch="$(date +%s)"
  "${server_bin}" "${config_path}" --smoke-seconds "${run_seconds}" >"${log_file}" 2>&1 &
  server_pid="$!"

  injected_kill=0
  while kill -0 "${server_pid}" >/dev/null 2>&1; do
    if (( attempt == 1 )) && (( injected_kill == 0 )) && [[ "${kill_after_first_seconds}" =~ ^[0-9]+$ ]] && (( kill_after_first_seconds > 0 )); then
      now_epoch="$(date +%s)"
      elapsed=$((now_epoch - start_epoch))
      if (( elapsed >= kill_after_first_seconds )); then
        kill -9 "${server_pid}" >/dev/null 2>&1 || true
        injected_kill=1
      fi
    fi
    sleep 0.1
  done

  set +e
  wait "${server_pid}"
  exit_code=$?
  set -e

  last_exit="${exit_code}"
  end_epoch="$(date +%s)"
  runtime_seconds=$((end_epoch - start_epoch))

  echo "${attempt},${exit_code},${runtime_seconds},$(basename "${log_file}"),${injected_kill}" >>"${attempt_metrics_csv}"

  old_attempt=$((attempt - rotation_keep))
  if (( old_attempt > 0 )); then
    rm -f "${out_dir}/server-attempt-${old_attempt}.log"
  fi

  if (( exit_code == 0 )); then
    success=1
    break
  fi

  if (( attempt > max_restarts )); then
    break
  fi

  restart_count=$((restart_count + 1))
  sleep "${backoff_seconds}"

done

run_end_epoch="$(date +%s)"
run_finished_at_utc="$(utc_now)"
elapsed_seconds=$((run_end_epoch - run_start_epoch))
status="$([[ "${success}" -eq 1 ]] && echo pass || echo fail)"

{
  echo "schema_version=1"
  echo "summary_kind=reliability_watchdog"
  echo "started_at_utc=${run_started_at_utc}"
  echo "finished_at_utc=${run_finished_at_utc}"
  echo "elapsed_seconds=${elapsed_seconds}"
  echo "status=${status}"
  echo "config=${config_path}"
  echo "out_dir=${out_dir}"
  echo "retention_policy=attempt_logs:max=${rotation_keep}"
  echo "attempt_metrics_csv=$(basename "${attempt_metrics_csv}")"
  echo "attempts=${attempt}"
  echo "restarts=${restart_count}"
  echo "max_restarts=${max_restarts}"
  echo "last_exit=${last_exit}"
  echo "run_seconds=${run_seconds}"
  echo "backoff_seconds=${backoff_seconds}"
  echo "rotation_keep=${rotation_keep}"
  echo "server_bin=${server_bin}"
} >"${summary_file}"

if (( success == 1 )); then
  echo "Watchdog run succeeded. Artifacts: ${out_dir}"
  exit 0
fi

echo "Watchdog run failed after ${attempt} attempt(s). Artifacts: ${out_dir}" >&2
exit 1
