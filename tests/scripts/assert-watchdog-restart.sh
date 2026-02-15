#!/usr/bin/env bash
set -euo pipefail

if [[ "$#" -lt 1 ]]; then
  echo "usage: $0 <repo-root>" >&2
  exit 2
fi

repo_root="$1"
out_dir="${repo_root}/artifacts/reliability/ctest-watchdog"
rm -rf "${out_dir}"

DEVY_WATCHDOG_KILL_FIRST_AFTER_SECONDS=1 \
  "${repo_root}/scripts/watchdog-server.sh" \
  "${repo_root}/config/server_test.json" \
  2 \
  2 \
  0.1 \
  "${out_dir}" \
  4

summary="${out_dir}/summary.txt"
if [[ ! -f "${summary}" ]]; then
  echo "Missing watchdog summary: ${summary}" >&2
  exit 1
fi

attempts="$(grep -E '^attempts=' "${summary}" | tail -n1 | cut -d= -f2)"
status="$(grep -E '^status=' "${summary}" | tail -n1 | cut -d= -f2)"

if [[ "${status}" != "success" ]]; then
  echo "Watchdog summary status is not success" >&2
  cat "${summary}"
  exit 1
fi

if [[ -z "${attempts}" ]] || (( attempts < 2 )); then
  echo "Expected watchdog to restart at least once" >&2
  cat "${summary}"
  exit 1
fi
