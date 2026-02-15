#!/usr/bin/env bash
set -euo pipefail

if [[ "$#" -lt 1 ]]; then
  echo "usage: $0 <repo-root>" >&2
  exit 2
fi

repo_root="$1"
out_dir="${repo_root}/artifacts/reliability/ctest-soak"
rm -rf "${out_dir}"

"${repo_root}/scripts/reliability-soak.sh" \
  "${repo_root}/config/server_test.json" \
  1 \
  4 \
  "${out_dir}" \
  3 \
  2 \
  2

summary="${out_dir}/summary.txt"
if [[ ! -f "${summary}" ]]; then
  echo "Missing soak summary: ${summary}" >&2
  exit 1
fi

if ! grep -q '^status=pass$' "${summary}"; then
  echo "Soak drill failed" >&2
  cat "${summary}"
  exit 1
fi
