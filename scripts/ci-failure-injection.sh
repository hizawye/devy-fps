#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
server_bin="${1:-${repo_root}/build/presets/debug-vcpkg/server/devy_server}"
invalid_config="${2:-${repo_root}/config/server_invalid_tick_rate.json}"
expected_message="${3:-Configuration validation failed}"

if [[ ! -x "${server_bin}" ]]; then
  echo "Server binary not found or not executable: ${server_bin}" >&2
  exit 1
fi

if [[ ! -f "${invalid_config}" ]]; then
  echo "Invalid-config fixture not found: ${invalid_config}" >&2
  exit 1
fi

output_log="$(mktemp)"
trap 'rm -f "${output_log}"' EXIT

set +e
"${server_bin}" "${invalid_config}" --smoke-seconds 1 > "${output_log}" 2>&1
status=$?
set -e

if [[ "${status}" -eq 0 ]]; then
  echo "Failure-injection check failed: invalid config unexpectedly succeeded." >&2
  cat "${output_log}" >&2
  exit 1
fi

if ! grep -q -- "${expected_message}" "${output_log}"; then
  echo "Failure-injection check failed: expected message not found (${expected_message})." >&2
  cat "${output_log}" >&2
  exit 1
fi

echo "Failure injection check passed (status=${status})."
