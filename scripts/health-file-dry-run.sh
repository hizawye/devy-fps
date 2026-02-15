#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

config_path="${1:-${DEVY_TEST_CONFIG_PATH:-${repo_root}/config/server_test.json}}"
seconds="${2:-2}"
out_dir="${3:-${repo_root}/artifacts/telemetry/health-file-dry-run/$(date +%Y%m%d-%H%M%S)}"
health_file="${4:-${out_dir}/runtime-health.json}"

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

server_bin="$(find_server_bin)"
if [[ -z "${server_bin}" ]]; then
  echo "Could not find devy_server binary. Set DEVY_SERVER_BIN or run scripts/build.sh first." >&2
  exit 1
fi

mkdir -p "${out_dir}"
server_log="${out_dir}/server.log"
summary_log="${out_dir}/summary.txt"

set +e
"${server_bin}" "${config_path}" --smoke-seconds "${seconds}" --health-file "${health_file}" \
  >"${server_log}" 2>&1
server_status=$?
set -e

health_exists=0
health_kind_ok=0
health_inbound_ok=0
health_players_ok=0
if [[ -f "${health_file}" ]]; then
  health_exists=1
  if grep -q '"kind":"runtime_diagnostics_v1"' "${health_file}"; then
    health_kind_ok=1
  fi
  if grep -q '"inbound":' "${health_file}"; then
    health_inbound_ok=1
  fi
  if grep -q '"players":' "${health_file}"; then
    health_players_ok=1
  fi
fi

status="pass"
if (( server_status != 0 )); then
  status="fail"
fi
if [[ "${health_exists}" -ne 1 || "${health_kind_ok}" -ne 1 || "${health_inbound_ok}" -ne 1 ||
      "${health_players_ok}" -ne 1 ]]; then
  status="fail"
fi

{
  echo "config=${config_path}"
  echo "seconds=${seconds}"
  echo "server_bin=${server_bin}"
  echo "server_status=${server_status}"
  echo "health_file=${health_file}"
  echo "health_exists=${health_exists}"
  echo "health_kind_ok=${health_kind_ok}"
  echo "health_inbound_ok=${health_inbound_ok}"
  echo "health_players_ok=${health_players_ok}"
  echo "status=${status}"
  echo
  echo "health_tail:"
  if [[ -f "${health_file}" ]]; then
    tail -n 1 "${health_file}" || true
  else
    echo "<missing>"
  fi
} >"${summary_log}"

if [[ "${status}" == "pass" ]]; then
  echo "Health-file dry run succeeded. Artifacts: ${out_dir}"
  exit 0
fi

echo "Health-file dry run failed. See ${summary_log}" >&2
exit 1
