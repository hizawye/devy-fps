#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

config_path="${1:-${repo_root}/config/server_test.json}"
clients="${2:-8}"
seconds="${3:-8}"
out_dir="${4:-${repo_root}/artifacts/reliability/chaos/$(date +%Y%m%d-%H%M%S)}"
malformed_rate_hz="${5:-30}"
disconnect_interval_ms="${6:-1200}"
malformed_family="${7:-mixed}"
malformed_burst_size="${8:-3}"

if [[ ! -f "${config_path}" && -f "${repo_root}/${config_path}" ]]; then
  config_path="${repo_root}/${config_path}"
fi

if [[ ! -f "${config_path}" ]]; then
  echo "Config file not found: ${config_path}" >&2
  exit 1
fi

if ! [[ "${malformed_burst_size}" =~ ^[0-9]+$ ]] || (( malformed_burst_size <= 0 )); then
  echo "malformed_burst_size must be a positive integer" >&2
  exit 1
fi

case "${malformed_family}" in
  legacy|mixed|invalid_json|unsupported_version|unknown_type|schema|envelope)
    ;;
  *)
    echo "malformed_family must be one of: legacy,mixed,invalid_json,unsupported_version,unknown_type,schema,envelope" >&2
    exit 1
    ;;
esac

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
server_log="${out_dir}/server.log"
client_log="${out_dir}/load-client.log"
summary_log="${out_dir}/summary.txt"

port="$(grep -E '"port"\s*:\s*[0-9]+' "${config_path}" | head -n1 | sed -E 's/[^0-9]*([0-9]+).*/\1/')"
if [[ -z "${port}" ]]; then
  port="7777"
fi

server_pid=""
cleanup() {
  if [[ -n "${server_pid}" ]] && kill -0 "${server_pid}" >/dev/null 2>&1; then
    kill "${server_pid}" >/dev/null 2>&1 || true
    wait "${server_pid}" >/dev/null 2>&1 || true
  fi
}
trap cleanup EXIT

server_smoke_seconds=$((seconds + 3))
"${server_bin}" "${config_path}" --smoke-seconds "${server_smoke_seconds}" >"${server_log}" 2>&1 &
server_pid="$!"

sleep 0.3

set +e
"${load_client_bin}" --host 127.0.0.1 --port "${port}" --clients "${clients}" --seconds "${seconds}" \
  --malformed-rate-hz "${malformed_rate_hz}" \
  --malformed-family "${malformed_family}" \
  --malformed-burst-size "${malformed_burst_size}" \
  --disconnect-interval-ms "${disconnect_interval_ms}" \
  --reconnect-delay-ms 150 >"${client_log}" 2>&1
client_status=$?
set -e

set +e
wait "${server_pid}"
server_status=$?
set -e
server_pid=""

invalid_drops="$(grep -c "Dropped invalid packet" "${server_log}" || true)"
invalid_json_drops="$(grep -c "Dropped invalid packet: invalid_json" "${server_log}" || true)"
invalid_envelope_type_drops="$(grep -c "Dropped invalid packet: invalid_envelope_field_type" "${server_log}" || true)"
unsupported_version_drops="$(grep -c "Dropped invalid packet: unsupported_version" "${server_log}" || true)"
unknown_type_drops="$(grep -c "Dropped invalid packet: unknown_message_type" "${server_log}" || true)"
invalid_payload_type_drops="$(grep -c "Dropped invalid packet: invalid_payload_type" "${server_log}" || true)"
missing_payload_field_drops="$(grep -c "Dropped invalid packet: missing_payload_field" "${server_log}" || true)"
forced_disconnects="$(grep -o 'forced_disconnects=[0-9]\+' "${client_log}" | tail -n1 | cut -d= -f2 || true)"
joined_total="$(grep -o 'joined=[0-9]\+' "${client_log}" | tail -n1 | cut -d= -f2 || true)"
malformed_sent="$(grep -o 'malformed_sent=[0-9]\+' "${client_log}" | tail -n1 | cut -d= -f2 || true)"

forced_disconnects="${forced_disconnects:-0}"
joined_total="${joined_total:-0}"
malformed_sent="${malformed_sent:-0}"

covered_error_categories=0
for value in \
  "${invalid_json_drops}" \
  "${invalid_envelope_type_drops}" \
  "${unsupported_version_drops}" \
  "${unknown_type_drops}" \
  "${invalid_payload_type_drops}" \
  "${missing_payload_field_drops}"; do
  if (( value > 0 )); then
    covered_error_categories=$((covered_error_categories + 1))
  fi
done

pass=1
if (( server_status != 0 )); then
  pass=0
fi
if (( client_status != 0 )); then
  pass=0
fi
if (( malformed_rate_hz > 0 )) && (( invalid_drops <= 0 )); then
  pass=0
fi
if (( malformed_rate_hz > 0 )); then
  case "${malformed_family}" in
    mixed)
      if (( covered_error_categories < 3 )); then
        pass=0
      fi
      ;;
    invalid_json)
      if (( invalid_json_drops <= 0 )); then
        pass=0
      fi
      ;;
    unsupported_version)
      if (( unsupported_version_drops <= 0 )); then
        pass=0
      fi
      ;;
    unknown_type)
      if (( unknown_type_drops <= 0 )); then
        pass=0
      fi
      ;;
    schema)
      if (( missing_payload_field_drops <= 0 )); then
        pass=0
      fi
      ;;
    legacy|envelope)
      if (( invalid_envelope_type_drops <= 0 )); then
        pass=0
      fi
      ;;
  esac
fi
if (( disconnect_interval_ms > 0 )) && (( forced_disconnects <= 0 )); then
  pass=0
fi
if (( joined_total <= 0 )); then
  pass=0
fi

{
  echo "config=${config_path}"
  echo "clients=${clients}"
  echo "seconds=${seconds}"
  echo "port=${port}"
  echo "malformed_rate_hz=${malformed_rate_hz}"
  echo "malformed_family=${malformed_family}"
  echo "malformed_burst_size=${malformed_burst_size}"
  echo "disconnect_interval_ms=${disconnect_interval_ms}"
  echo "server_bin=${server_bin}"
  echo "load_client_bin=${load_client_bin}"
  echo "server_status=${server_status}"
  echo "client_status=${client_status}"
  echo "invalid_packet_drops=${invalid_drops}"
  echo "invalid_json_drops=${invalid_json_drops}"
  echo "invalid_envelope_field_type_drops=${invalid_envelope_type_drops}"
  echo "unsupported_version_drops=${unsupported_version_drops}"
  echo "unknown_message_type_drops=${unknown_type_drops}"
  echo "invalid_payload_type_drops=${invalid_payload_type_drops}"
  echo "missing_payload_field_drops=${missing_payload_field_drops}"
  echo "covered_error_categories=${covered_error_categories}"
  echo "forced_disconnects=${forced_disconnects}"
  echo "joined_total=${joined_total}"
  echo "malformed_sent=${malformed_sent}"
  echo "status=$([[ "${pass}" -eq 1 ]] && echo pass || echo fail)"
  echo
  echo "load_client_output:"
  sed -n '1,10p' "${client_log}"
} >"${summary_log}"

if (( pass == 1 )); then
  echo "Chaos drill succeeded. Artifacts: ${out_dir}"
  exit 0
fi

echo "Chaos drill failed. See ${summary_log}" >&2
exit 1
