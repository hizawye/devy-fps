#!/usr/bin/env bash
set -euo pipefail

if [[ "$#" -lt 1 ]]; then
  echo "usage: $0 <repo-root>" >&2
  exit 2
fi

repo_root="$1"
config_path="${DEVY_TEST_CONFIG_PATH:-${repo_root}/config/server_test.json}"
out_dir="${repo_root}/artifacts/reliability/ctest-watchdog"
rm -rf "${out_dir}"

DEVY_WATCHDOG_KILL_FIRST_AFTER_SECONDS=1 \
  "${repo_root}/scripts/watchdog-server.sh" \
  "${config_path}" \
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
schema_version="$(grep -E '^schema_version=' "${summary}" | tail -n1 | cut -d= -f2)"
summary_kind="$(grep -E '^summary_kind=' "${summary}" | tail -n1 | cut -d= -f2)"

if [[ "${schema_version}" != "1" ]]; then
  echo "Watchdog summary schema_version is not 1" >&2
  cat "${summary}"
  exit 1
fi

if [[ "${summary_kind}" != "reliability_watchdog" ]]; then
  echo "Watchdog summary_kind is not reliability_watchdog" >&2
  cat "${summary}"
  exit 1
fi

if [[ "${status}" != "pass" ]]; then
  echo "Watchdog summary status is not pass" >&2
  cat "${summary}"
  exit 1
fi

if [[ -z "${attempts}" ]] || (( attempts < 2 )); then
  echo "Expected watchdog to restart at least once" >&2
  cat "${summary}"
  exit 1
fi
