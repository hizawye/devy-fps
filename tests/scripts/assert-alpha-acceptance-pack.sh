#!/usr/bin/env bash
set -euo pipefail

if [[ "$#" -lt 1 ]]; then
  echo "usage: $0 <repo-root>" >&2
  exit 2
fi

repo_root="$1"
out_dir="${repo_root}/artifacts/releases/ctest-alpha-acceptance"
config_path="${DEVY_TEST_CONFIG_PATH:-${repo_root}/config/server_test.json}"
rm -rf "${out_dir}"

DEVY_SKIP_BUILD=1 "${repo_root}/scripts/alpha-acceptance-pack.sh" \
  "${config_path}" \
  "${out_dir}" \
  debug-vcpkg \
  6 \
  4 \
  0

summary="${out_dir}/summary.txt"
if [[ ! -f "${summary}" ]]; then
  echo "Missing alpha acceptance summary: ${summary}" >&2
  exit 1
fi

if ! grep -q '^status=pass$' "${summary}"; then
  echo "Alpha acceptance pack failed" >&2
  cat "${summary}"
  exit 1
fi
