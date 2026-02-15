#!/usr/bin/env bash
set -euo pipefail

if [[ "$#" -lt 1 ]]; then
  echo "usage: $0 <repo-root> [seconds]" >&2
  exit 2
fi

repo_root="$1"
seconds="${2:-1}"
config_path="${DEVY_TEST_CONFIG_PATH:-${repo_root}/config/server_test.json}"
server_bin="${DEVY_SERVER_BIN:-}"

if [[ -z "${server_bin}" ]]; then
  candidates=(
    "${repo_root}/build/presets/debug/server/devy_server"
    "${repo_root}/build/presets/debug-vcpkg/server/devy_server"
    "${repo_root}/build/presets/release/server/devy_server"
    "${repo_root}/build/presets/release-vcpkg/server/devy_server"
    "${repo_root}/build/debug/server/devy_server"
    "${repo_root}/build/debug/devy_server"
    "${repo_root}/build/server/devy_server"
    "${repo_root}/build/devy_server"
  )
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

DEVY_SERVER_BIN="${server_bin}" "${repo_root}/scripts/smoke-server.sh" "${config_path}" "${seconds}"
