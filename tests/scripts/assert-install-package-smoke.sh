#!/usr/bin/env bash
set -euo pipefail

if [[ "$#" -lt 1 ]]; then
  echo "usage: $0 <repo-root>" >&2
  exit 2
fi

repo_root="$1"
out_dir="${repo_root}/artifacts/releases/ctest-install-smoke"
rm -rf "${out_dir}"

DEVY_SKIP_BUILD=1 "${repo_root}/scripts/install-package-smoke.sh" \
  debug-vcpkg \
  "${out_dir}" \
  4 \
  3 \
  3

summary="${out_dir}/summary.txt"
if [[ ! -f "${summary}" ]]; then
  echo "Missing install smoke summary: ${summary}" >&2
  exit 1
fi

if ! grep -q '^status=pass$' "${summary}"; then
  echo "Install package smoke failed" >&2
  cat "${summary}"
  exit 1
fi
