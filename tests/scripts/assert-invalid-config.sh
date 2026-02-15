#!/usr/bin/env bash
set -euo pipefail

if [[ "$#" -lt 2 ]]; then
  echo "usage: $0 <server-bin> <config-path> [expected-substring]" >&2
  exit 2
fi

server_bin="$1"
config_path="$2"
expected_substring="${3:-Configuration validation failed}"
log_file="$(mktemp)"
trap 'rm -f "${log_file}"' EXIT

set +e
"${server_bin}" "${config_path}" --smoke-seconds 1 >"${log_file}" 2>&1
status=$?
set -e

cat "${log_file}"

if [[ "${status}" -eq 0 ]]; then
  echo "Expected non-zero exit status for invalid config: ${config_path}" >&2
  exit 1
fi

if ! grep -q "${expected_substring}" "${log_file}"; then
  echo "Expected failure message '${expected_substring}' for config: ${config_path}" >&2
  exit 1
fi
