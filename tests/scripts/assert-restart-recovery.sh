#!/usr/bin/env bash
set -euo pipefail

if [[ "$#" -lt 1 ]]; then
  echo "usage: $0 <repo-root>" >&2
  exit 2
fi

repo_root="$1"
out_dir="${repo_root}/artifacts/reliability/ctest-restart-recovery"
rm -rf "${out_dir}"

"${repo_root}/scripts/restart-recovery.sh" \
  "${repo_root}/config/server_test.json" \
  4 \
  2 \
  "${out_dir}"

summary="${out_dir}/summary.txt"
if [[ ! -f "${summary}" ]]; then
  echo "Missing restart recovery summary: ${summary}" >&2
  exit 1
fi

if ! grep -q '^status=pass$' "${summary}"; then
  echo "Restart recovery drill failed" >&2
  cat "${summary}"
  exit 1
fi
