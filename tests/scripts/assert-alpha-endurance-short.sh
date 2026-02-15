#!/usr/bin/env bash
set -euo pipefail

if [[ "$#" -lt 1 ]]; then
  echo "usage: $0 <repo-root>" >&2
  exit 2
fi

repo_root="$1"
out_dir="${repo_root}/artifacts/releases/ctest-alpha-endurance-short"
config_path="${DEVY_TEST_CONFIG_PATH:-${repo_root}/config/server_test.json}"
rm -rf "${out_dir}"

(
  cd "${repo_root}"
  "${repo_root}/scripts/alpha-endurance-run.sh" \
  "${config_path}" \
  1 \
  6 \
  "${out_dir}" \
  8 \
  4 \
  3
)

summary="${out_dir}/summary.txt"
if [[ ! -f "${summary}" ]]; then
  echo "Missing alpha endurance summary: ${summary}" >&2
  exit 1
fi

if ! grep -q '^status=pass$' "${summary}"; then
  echo "Alpha endurance short check failed" >&2
  cat "${summary}"
  exit 1
fi
