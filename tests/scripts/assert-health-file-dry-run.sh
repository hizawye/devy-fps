#!/usr/bin/env bash
set -euo pipefail

if [[ "$#" -lt 1 ]]; then
  echo "usage: $0 <repo-root>" >&2
  exit 2
fi

repo_root="$1"
out_dir="${repo_root}/artifacts/telemetry/ctest-health-file-dry-run"
rm -rf "${out_dir}"

"${repo_root}/scripts/health-file-dry-run.sh" \
  "${DEVY_TEST_CONFIG_PATH:-${repo_root}/config/server_test.json}" \
  2 \
  "${out_dir}" \
  "${out_dir}/runtime-health.json"

summary="${out_dir}/summary.txt"
if [[ ! -f "${summary}" ]]; then
  echo "Missing health-file summary: ${summary}" >&2
  exit 1
fi

if ! grep -q '^status=pass$' "${summary}"; then
  echo "Health-file dry run failed" >&2
  cat "${summary}"
  exit 1
fi
