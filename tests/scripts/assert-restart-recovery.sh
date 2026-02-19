#!/usr/bin/env bash
set -euo pipefail

if [[ "$#" -lt 1 ]]; then
  echo "usage: $0 <repo-root>" >&2
  exit 2
fi

repo_root="$1"
config_path="${DEVY_TEST_CONFIG_PATH:-${repo_root}/config/server_test.json}"
out_dir="${repo_root}/artifacts/reliability/ctest-restart-recovery"
rm -rf "${out_dir}"

"${repo_root}/scripts/restart-recovery.sh" \
  "${config_path}" \
  4 \
  2 \
  "${out_dir}"

summary="${out_dir}/summary.txt"
if [[ ! -f "${summary}" ]]; then
  echo "Missing restart recovery summary: ${summary}" >&2
  exit 1
fi

if ! grep -q '^schema_version=1$' "${summary}"; then
  echo "Restart recovery summary missing schema_version=1" >&2
  cat "${summary}"
  exit 1
fi

if ! grep -q '^summary_kind=reliability_restart_recovery$' "${summary}"; then
  echo "Restart recovery summary missing summary_kind" >&2
  cat "${summary}"
  exit 1
fi

if ! grep -q '^status=pass$' "${summary}"; then
  echo "Restart recovery drill failed" >&2
  cat "${summary}"
  exit 1
fi
