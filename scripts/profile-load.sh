#!/usr/bin/env bash
set -euo pipefail

config_path="${1:-config/server_test.json}"
clients="${2:-8}"
seconds="${3:-8}"
out_dir="${4:-artifacts/telemetry/$(date +%Y%m%d-%H%M%S)}"

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
    "build/presets/debug/server/devy_server"
    "build/presets/debug-vcpkg/server/devy_server"
    "build/presets/release/server/devy_server"
    "build/presets/release-vcpkg/server/devy_server"
    "build/debug/server/devy_server"
    "build/debug/devy_server"
    "build/server/devy_server"
    "build/devy_server"
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
    "build/presets/debug/server/devy_load_client"
    "build/presets/debug-vcpkg/server/devy_load_client"
    "build/presets/release/server/devy_load_client"
    "build/presets/release-vcpkg/server/devy_load_client"
    "build/debug/server/devy_load_client"
    "build/server/devy_load_client"
    "build/devy_load_client"
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
telemetry_log="${out_dir}/telemetry.log"
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

"${server_bin}" "${config_path}" --smoke-seconds "${seconds}" >"${server_log}" 2>&1 &
server_pid="$!"

# Give the server a brief head start before clients connect.
sleep 0.3

"${load_client_bin}" --host 127.0.0.1 --port "${port}" --clients "${clients}" --seconds "${seconds}" \
  >"${client_log}" 2>&1

wait "${server_pid}"
server_pid=""

grep -F "Runtime profile tick=" "${server_log}" >"${telemetry_log}" || true

{
  echo "config=${config_path}"
  echo "clients=${clients}"
  echo "seconds=${seconds}"
  echo "port=${port}"
  echo "server_bin=${server_bin}"
  echo "load_client_bin=${load_client_bin}"
  echo
  echo "load_client_output:"
  sed -n '1,4p' "${client_log}"
  echo

  telemetry_count="$(wc -l < "${telemetry_log}" | tr -d ' ')"
  echo "telemetry_reports=${telemetry_count}"

  if [[ "${telemetry_count}" -eq 0 ]]; then
    echo "No telemetry report lines were captured."
  else
    awk '
      function trim(s) { gsub(/^[[:space:]]+|[[:space:]]+$/, "", s); return s }
      {
        if (index($0, "Runtime profile tick=") == 0) {
          next
        }

        reports += 1

        if (match($0, /phases_ms\{[^}]*\}/)) {
          section = substr($0, RSTART + 10, RLENGTH - 11)
          count = split(section, raw_parts, ",")
          for (i = 1; i <= count; ++i) {
            part = trim(raw_parts[i])
            split(part, kv, "=")
            if (length(kv) < 2) {
              continue
            }
            phase = trim(kv[1])
            split(kv[2], phase_vals, "/")
            phase_sum[phase] += phase_vals[1] + 0.0
            phase_count[phase] += 1
          }
        }

        if (match($0, /top=[a-z_]+:[0-9]+/)) {
          top_entry = substr($0, RSTART + 4, RLENGTH - 4)
          split(top_entry, top_parts, ":")
          if (length(top_parts) == 2) {
            msg = trim(top_parts[1])
            bytes = top_parts[2] + 0
            top_sum[msg] += bytes
          }
        }

        latest = $0
      }
      END {
        print "analysis:"
        if (reports == 0) {
          print "  no reports to analyze"
          exit
        }

        top_phase = "n/a"
        top_phase_avg = -1.0
        for (phase in phase_sum) {
          avg = phase_sum[phase] / phase_count[phase]
          if (avg > top_phase_avg) {
            top_phase_avg = avg
            top_phase = phase
          }
        }

        top_msg = "n/a"
        top_msg_bytes = -1
        for (msg in top_sum) {
          if (top_sum[msg] > top_msg_bytes) {
            top_msg_bytes = top_sum[msg]
            top_msg = msg
          }
        }

        printf("  highest_avg_phase=%s avg_ms=%.3f\n", top_phase, top_phase_avg)
        printf("  highest_total_top_message=%s total_bytes=%d\n", top_msg, top_msg_bytes)
        printf("  latest_report=%s\n", latest)
      }
    ' "${telemetry_log}"
  fi
} >"${summary_log}"

echo "Load profile artifacts written to ${out_dir}"
