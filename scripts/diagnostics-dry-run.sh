#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

config_path="${1:-${repo_root}/config/server_diagnostics_alert.json}"
seconds="${2:-2}"
out_dir="${3:-${repo_root}/artifacts/telemetry/diagnostics-dry-run/$(date +%Y%m%d-%H%M%S)}"
expected_alert="${4:-active_players_below_min}"

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
diagnostics_log="${out_dir}/diagnostics.log"
summary_log="${out_dir}/summary.txt"

set +e
"${server_bin}" "${config_path}" --smoke-seconds "${seconds}" >"${server_log}" 2>&1
server_status=$?
set -e

grep -F "Runtime diagnostics json=" "${server_log}" >"${diagnostics_log}" || true

diagnostics_reports="$(wc -l < "${diagnostics_log}" | tr -d ' ')"
alert_hits=0
if [[ -n "${expected_alert}" ]]; then
  alert_hits="$(grep -c "\"${expected_alert}\"" "${diagnostics_log}" || true)"
fi

required_keys_ok=1
if [[ "${diagnostics_reports}" -eq 0 ]]; then
  required_keys_ok=0
else
  if ! grep -q '"kind":"runtime_diagnostics_v1"' "${diagnostics_log}"; then
    required_keys_ok=0
  fi
  if ! grep -q '"inbound":' "${diagnostics_log}"; then
    required_keys_ok=0
  fi
  if ! grep -q '"players":' "${diagnostics_log}"; then
    required_keys_ok=0
  fi
fi

status="pass"
if (( server_status != 0 )); then
  status="fail"
fi
if [[ "${diagnostics_reports}" -eq 0 ]]; then
  status="fail"
fi
if [[ "${required_keys_ok}" -ne 1 ]]; then
  status="fail"
fi
if [[ -n "${expected_alert}" && "${alert_hits}" -eq 0 ]]; then
  status="fail"
fi

{
  echo "config=${config_path}"
  echo "seconds=${seconds}"
  echo "server_bin=${server_bin}"
  echo "server_status=${server_status}"
  echo "diagnostics_reports=${diagnostics_reports}"
  echo "required_keys_ok=${required_keys_ok}"
  echo "expected_alert=${expected_alert}"
  echo "expected_alert_hits=${alert_hits}"
  echo "status=${status}"
  echo
  echo "latest_diagnostics:"
  tail -n 1 "${diagnostics_log}" || true
} >"${summary_log}"

if [[ "${status}" == "pass" ]]; then
  echo "Diagnostics dry run succeeded. Artifacts: ${out_dir}"
  exit 0
fi

echo "Diagnostics dry run failed. See ${summary_log}" >&2
exit 1
