#!/usr/bin/env bash
set -euo pipefail

if [[ "$#" -lt 1 ]]; then
  echo "usage: $0 <repo-root>" >&2
  exit 2
fi

repo_root="$1"
config_path="${DEVY_TEST_CONFIG_PATH:-${repo_root}/config/server_test.json}"
out_dir="${repo_root}/artifacts/reliability/ctest-chaos"
rm -rf "${out_dir}"

"${repo_root}/scripts/chaos-drill.sh" \
  "${config_path}" \
  4 \
  3 \
  "${out_dir}" \
  20 \
  700 \
  mixed \
  3

summary="${out_dir}/summary.txt"
if [[ ! -f "${summary}" ]]; then
  echo "Missing chaos summary: ${summary}" >&2
  exit 1
fi

if ! grep -q '^status=pass$' "${summary}"; then
  echo "Chaos drill failed" >&2
  cat "${summary}"
  exit 1
fi
