#!/usr/bin/env bash
set -euo pipefail

if [[ "$#" -lt 1 ]]; then
  echo "usage: $0 <repo-root>" >&2
  exit 2
fi

repo_root="$1"
out_dir="${repo_root}/artifacts/releases/ctest-rollback-rehearsal"
config_path="${DEVY_TEST_CONFIG_PATH:-}"
rm -rf "${out_dir}"

DEVY_SKIP_BUILD=1 "${repo_root}/scripts/rollback-rehearsal.sh" \
  debug-vcpkg \
  "${out_dir}" \
  4 \
  3 \
  "${config_path}"

summary="${out_dir}/summary.txt"
if [[ ! -f "${summary}" ]]; then
  echo "Missing rollback rehearsal summary: ${summary}" >&2
  exit 1
fi

if ! grep -q '^status=pass$' "${summary}"; then
  echo "Rollback rehearsal failed" >&2
  cat "${summary}"
  exit 1
fi
