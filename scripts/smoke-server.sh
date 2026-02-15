#!/usr/bin/env bash
set -euo pipefail

config="${1:-config/server_test.json}"
seconds="${2:-2}"

if [[ -n "${DEVY_SERVER_BIN:-}" ]]; then
  server_bin="${DEVY_SERVER_BIN}"
else
  candidates=(
    "build/presets/debug/server/devy_server"
    "build/presets/debug-vcpkg/server/devy_server"
    "build/presets/release/server/devy_server"
    "build/presets/release-vcpkg/server/devy_server"
    "build/debug/server/devy_server"
    "build/debug/devy_server"
    "build/server/devy_server"
    "build/devy_server"
  )
  server_bin=""
  for candidate in "${candidates[@]}"; do
    if [[ -x "${candidate}" ]]; then
      server_bin="${candidate}"
      break
    fi
  done
fi

if [[ -z "${server_bin}" ]]; then
  echo "Could not find devy_server binary. Set DEVY_SERVER_BIN or run scripts/build.sh first." >&2
  exit 1
fi

"${server_bin}" "${config}" --smoke-seconds "${seconds}"
