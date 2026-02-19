#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

config_path="${1:-${repo_root}/config/server_port_fallback_smoke.json}"
seconds="${2:-2}"
out_dir="${3:-${repo_root}/artifacts/telemetry/port-fallback-smoke/$(date +%Y%m%d-%H%M%S)}"

if [[ ! -f "${config_path}" && -f "${repo_root}/${config_path}" ]]; then
  config_path="${repo_root}/${config_path}"
fi

if [[ ! -f "${config_path}" ]]; then
  echo "Config file not found: ${config_path}" >&2
  exit 1
fi

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

wait_for_log_line() {
  local log_file="$1"
  local needle="$2"
  local attempts="$3"

  local i
  for ((i = 0; i < attempts; ++i)); do
    if [[ -f "${log_file}" ]] && grep -q "${needle}" "${log_file}"; then
      return 0
    fi
    sleep 0.05
  done
  return 1
}

server_bin="$(find_server_bin)"
if [[ -z "${server_bin}" ]]; then
  echo "Could not find devy_server binary. Set DEVY_SERVER_BIN or run scripts/build.sh first." >&2
  exit 1
fi

mkdir -p "${out_dir}"
primary_log="${out_dir}/primary.log"
fallback_log="${out_dir}/fallback.log"
summary_log="${out_dir}/summary.txt"
runtime_file="${repo_root}/runtime/server-port-fallback-smoke.json"

rm -f "${runtime_file}"

primary_pid=""
fallback_pid=""
cleanup() {
  if [[ -n "${fallback_pid}" ]] && kill -0 "${fallback_pid}" 2>/dev/null; then
    kill "${fallback_pid}" 2>/dev/null || true
    wait "${fallback_pid}" 2>/dev/null || true
  fi
  if [[ -n "${primary_pid}" ]] && kill -0 "${primary_pid}" 2>/dev/null; then
    kill "${primary_pid}" 2>/dev/null || true
    wait "${primary_pid}" 2>/dev/null || true
  fi
}
trap cleanup EXIT

(
  cd "${repo_root}"
  "${server_bin}" "${repo_root}/config/server_test.json" --smoke-seconds 8 >"${primary_log}" 2>&1
) &
primary_pid="$!"

primary_started=0
if wait_for_log_line "${primary_log}" "Server started on port 17777\." 200; then
  primary_started=1
fi

(
  cd "${repo_root}"
  "${server_bin}" "${config_path}" --smoke-seconds "${seconds}" >"${fallback_log}" 2>&1
) &
fallback_pid="$!"

fallback_started=0
fallback_log_has_retry=0
runtime_file_exists_during_run=0
runtime_file_has_fallback_port=0
runtime_file_removed_after_shutdown=0
fallback_status=1

if wait_for_log_line "${fallback_log}" "Server started on port 17778\." 260; then
  fallback_started=1
fi

if [[ -f "${fallback_log}" ]] &&
    grep -q "Primary configured port 17777 unavailable; using port 17778\." "${fallback_log}"; then
  fallback_log_has_retry=1
fi

if [[ -f "${runtime_file}" ]]; then
  runtime_file_exists_during_run=1
  if grep -q '"port":17778' "${runtime_file}"; then
    runtime_file_has_fallback_port=1
  fi
fi

set +e
wait "${fallback_pid}"
fallback_status=$?
set -e
fallback_pid=""

# Let cleanup race settle after process exit.
sleep 0.1
if [[ ! -f "${runtime_file}" ]]; then
  runtime_file_removed_after_shutdown=1
fi

set +e
wait "${primary_pid}" >/dev/null 2>&1
set -e
primary_pid=""

status="pass"
if [[ "${primary_started}" -ne 1 ]]; then
  status="fail"
fi
if [[ "${fallback_status}" -ne 0 ]]; then
  status="fail"
fi
if [[ "${fallback_started}" -ne 1 ]]; then
  status="fail"
fi
if [[ "${fallback_log_has_retry}" -ne 1 ]]; then
  status="fail"
fi
if [[ "${runtime_file_exists_during_run}" -ne 1 || "${runtime_file_has_fallback_port}" -ne 1 ]]; then
  status="fail"
fi
if [[ "${runtime_file_removed_after_shutdown}" -ne 1 ]]; then
  status="fail"
fi

{
  echo "config=${config_path}"
  echo "seconds=${seconds}"
  echo "server_bin=${server_bin}"
  echo "primary_started=${primary_started}"
  echo "fallback_status=${fallback_status}"
  echo "fallback_started=${fallback_started}"
  echo "fallback_log_has_retry=${fallback_log_has_retry}"
  echo "runtime_file=${runtime_file}"
  echo "runtime_file_exists_during_run=${runtime_file_exists_during_run}"
  echo "runtime_file_has_fallback_port=${runtime_file_has_fallback_port}"
  echo "runtime_file_removed_after_shutdown=${runtime_file_removed_after_shutdown}"
  echo "status=${status}"
  echo
  echo "fallback_log_tail:"
  tail -n 20 "${fallback_log}" || true
} >"${summary_log}"

if [[ "${status}" == "pass" ]]; then
  echo "Port fallback smoke succeeded. Artifacts: ${out_dir}"
  exit 0
fi

echo "Port fallback smoke failed. See ${summary_log}" >&2
cat "${summary_log}" >&2
exit 1
