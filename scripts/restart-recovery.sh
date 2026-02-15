#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

config_path="${1:-${repo_root}/config/server_test.json}"
clients="${2:-6}"
phase_seconds="${3:-4}"
out_dir="${4:-${repo_root}/artifacts/reliability/restart-recovery/$(date +%Y%m%d-%H%M%S)}"

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

find_load_client_bin() {
  if [[ -n "${DEVY_LOAD_CLIENT_BIN:-}" ]]; then
    echo "${DEVY_LOAD_CLIENT_BIN}"
    return
  fi

  local candidates=(
    "${repo_root}/build/presets/debug/server/devy_load_client"
    "${repo_root}/build/presets/debug-vcpkg/server/devy_load_client"
    "${repo_root}/build/presets/release/server/devy_load_client"
    "${repo_root}/build/presets/release-vcpkg/server/devy_load_client"
    "${repo_root}/build/debug/server/devy_load_client"
    "${repo_root}/build/server/devy_load_client"
    "${repo_root}/build/devy_load_client"
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

load_client_bin="$(find_load_client_bin)"
if [[ -z "${load_client_bin}" ]]; then
  echo "Could not find devy_load_client binary. Build project first." >&2
  exit 1
fi

mkdir -p "${out_dir}"
server_phase1_log="${out_dir}/server-phase1.log"
server_phase2_log="${out_dir}/server-phase2.log"
client_phase1_log="${out_dir}/load-client-phase1.log"
client_phase2_log="${out_dir}/load-client-phase2.log"
summary_log="${out_dir}/summary.txt"

port="$(grep -E '"port"\s*:\s*[0-9]+' "${config_path}" | head -n1 | sed -E 's/[^0-9]*([0-9]+).*/\1/')"
if [[ -z "${port}" ]]; then
  port="7777"
fi

run_load_phase() {
  local log_file="$1"
  set +e
  "${load_client_bin}" --host 127.0.0.1 --port "${port}" --clients "${clients}" --seconds "${phase_seconds}" >"${log_file}" 2>&1
  local status=$?
  set -e
  echo "${status}"
}

extract_joined() {
  local log_file="$1"
  local joined
  joined="$(grep -o 'joined=[0-9]\+' "${log_file}" | tail -n1 | cut -d= -f2 || true)"
  echo "${joined:-0}"
}

phase_smoke_seconds=$((phase_seconds + 5))

"${server_bin}" "${config_path}" --smoke-seconds "${phase_smoke_seconds}" >"${server_phase1_log}" 2>&1 &
server_pid_phase1="$!"
sleep 0.3
phase1_status="$(run_load_phase "${client_phase1_log}")"
phase1_joined="$(extract_joined "${client_phase1_log}")"

kill "${server_pid_phase1}" >/dev/null 2>&1 || true
set +e
wait "${server_pid_phase1}"
server_phase1_status=$?
set -e

"${server_bin}" "${config_path}" --smoke-seconds "${phase_smoke_seconds}" >"${server_phase2_log}" 2>&1 &
server_pid_phase2="$!"
sleep 0.3
phase2_status="$(run_load_phase "${client_phase2_log}")"
phase2_joined="$(extract_joined "${client_phase2_log}")"

set +e
wait "${server_pid_phase2}"
server_phase2_status=$?
set -e

pass=1
if (( phase1_status != 0 )) || (( phase2_status != 0 )); then
  pass=0
fi
if (( phase1_joined <= 0 )) || (( phase2_joined <= 0 )); then
  pass=0
fi
if (( server_phase2_status != 0 )); then
  pass=0
fi

{
  echo "config=${config_path}"
  echo "clients=${clients}"
  echo "phase_seconds=${phase_seconds}"
  echo "port=${port}"
  echo "server_bin=${server_bin}"
  echo "load_client_bin=${load_client_bin}"
  echo "phase1_status=${phase1_status}"
  echo "phase1_joined=${phase1_joined}"
  echo "phase1_server_status=${server_phase1_status}"
  echo "phase2_status=${phase2_status}"
  echo "phase2_joined=${phase2_joined}"
  echo "phase2_server_status=${server_phase2_status}"
  echo "status=$([[ "${pass}" -eq 1 ]] && echo pass || echo fail)"
  echo
  echo "phase1_client_output:"
  sed -n '1,10p' "${client_phase1_log}"
  echo
  echo "phase2_client_output:"
  sed -n '1,10p' "${client_phase2_log}"
} >"${summary_log}"

if (( pass == 1 )); then
  echo "Restart recovery drill succeeded. Artifacts: ${out_dir}"
  exit 0
fi

echo "Restart recovery drill failed. See ${summary_log}" >&2
exit 1
