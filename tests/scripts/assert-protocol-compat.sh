#!/usr/bin/env bash
set -euo pipefail

if [[ "$#" -lt 1 ]]; then
  echo "usage: $0 <repo-root>" >&2
  exit 2
fi

repo_root="$1"
out_dir="${repo_root}/artifacts/releases/ctest-protocol-compat"
rm -rf "${out_dir}"

"${repo_root}/scripts/protocol-compat-check.sh" \
  "${repo_root}/config/server_test.json" \
  6 \
  4 \
  "${out_dir}" \
  16 \
  2

summary="${out_dir}/summary.txt"
if [[ ! -f "${summary}" ]]; then
  echo "Missing protocol-compat summary: ${summary}" >&2
  exit 1
fi

if ! grep -q '^status=pass$' "${summary}"; then
  echo "Protocol compatibility check failed" >&2
  cat "${summary}"
  exit 1
fi
