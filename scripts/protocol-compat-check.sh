#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

config_path="${1:-${repo_root}/config/server_test.json}"
clients="${2:-6}"
seconds="${3:-4}"
out_dir="${4:-${repo_root}/artifacts/releases/protocol-compat/$(date +%Y%m%d-%H%M%S)}"
malformed_rate_hz="${5:-16}"
malformed_burst_size="${6:-2}"

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
load_client_bin="$(find_load_client_bin)"

if [[ -z "${server_bin}" ]]; then
  echo "Could not find devy_server binary. Build project first or set DEVY_SERVER_BIN." >&2
  exit 1
fi
if [[ -z "${load_client_bin}" ]]; then
  echo "Could not find devy_load_client binary. Build project first or set DEVY_LOAD_CLIENT_BIN." >&2
  exit 1
fi

mkdir -p "${out_dir}"
server_log="${out_dir}/server.log"
load_log="${out_dir}/load-client.log"
summary_log="${out_dir}/summary.txt"

port="$(grep -E '"port"\s*:\s*[0-9]+' "${config_path}" | head -n1 | sed -E 's/[^0-9]*([0-9]+).*/\1/')"
if [[ -z "${port}" ]]; then
  port="17777"
fi

server_pid=""
cleanup() {
  if [[ -n "${server_pid}" ]] && kill -0 "${server_pid}" >/dev/null 2>&1; then
    kill "${server_pid}" >/dev/null 2>&1 || true
    wait "${server_pid}" >/dev/null 2>&1 || true
  fi
}
trap cleanup EXIT

server_smoke_seconds=$((seconds + 4))
"${server_bin}" "${config_path}" --smoke-seconds "${server_smoke_seconds}" >"${server_log}" 2>&1 &
server_pid="$!"

sleep 0.3
set +e
"${load_client_bin}" --host 127.0.0.1 --port "${port}" --clients "${clients}" --seconds "${seconds}" \
  --malformed-rate-hz "${malformed_rate_hz}" \
  --malformed-family unsupported_version \
  --malformed-burst-size "${malformed_burst_size}" >"${load_log}" 2>&1
load_status=$?
set -e

set +e
wait "${server_pid}"
server_status=$?
set -e
server_pid=""

joined_total="$(grep -o 'joined=[0-9]\+' "${load_log}" | tail -n1 | cut -d= -f2 || true)"
joined_total="${joined_total:-0}"
malformed_sent="$(grep -o 'malformed_sent=[0-9]\+' "${load_log}" | tail -n1 | cut -d= -f2 || true)"
malformed_sent="${malformed_sent:-0}"
unsupported_version_drops="$(grep -c 'Dropped invalid packet: unsupported_version' "${server_log}" || true)"

pass=1
if (( server_status != 0 )) || (( load_status != 0 )); then
  pass=0
fi
if (( joined_total <= 0 )) || (( malformed_sent <= 0 )) || (( unsupported_version_drops <= 0 )); then
  pass=0
fi

{
  echo "config=${config_path}"
  echo "port=${port}"
  echo "clients=${clients}"
  echo "seconds=${seconds}"
  echo "malformed_rate_hz=${malformed_rate_hz}"
  echo "malformed_burst_size=${malformed_burst_size}"
  echo "server_bin=${server_bin}"
  echo "load_client_bin=${load_client_bin}"
  echo "server_status=${server_status}"
  echo "load_status=${load_status}"
  echo "joined_total=${joined_total}"
  echo "malformed_sent=${malformed_sent}"
  echo "unsupported_version_drops=${unsupported_version_drops}"
  echo "status=$([[ "${pass}" -eq 1 ]] && echo pass || echo fail)"
  echo
  echo "load_client_output:"
  sed -n '1,12p' "${load_log}"
} >"${summary_log}"

if (( pass == 1 )); then
  echo "Protocol compatibility check passed. Artifacts: ${out_dir}"
  exit 0
fi

echo "Protocol compatibility check failed. See ${summary_log}" >&2
exit 1
