#!/usr/bin/env bash
set -euo pipefail

if [[ "$#" -lt 1 ]]; then
  echo "usage: $0 <repo-root>" >&2
  exit 2
fi

repo_root="$1"
out_dir="${repo_root}/artifacts/telemetry/ctest-port-fallback-smoke"
rm -rf "${out_dir}"

"${repo_root}/scripts/port-fallback-smoke.sh" \
  "${DEVY_TEST_CONFIG_PATH:-${repo_root}/config/server_port_fallback_smoke.json}" \
  2 \
  "${out_dir}"

summary="${out_dir}/summary.txt"
if [[ ! -f "${summary}" ]]; then
  echo "Missing fallback summary: ${summary}" >&2
  exit 1
fi

if ! grep -q '^status=pass$' "${summary}"; then
  echo "Port fallback smoke failed" >&2
  cat "${summary}"
  exit 1
fi
