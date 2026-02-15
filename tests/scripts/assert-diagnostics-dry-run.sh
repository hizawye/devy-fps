#!/usr/bin/env bash
set -euo pipefail

if [[ "$#" -lt 1 ]]; then
  echo "usage: $0 <repo-root>" >&2
  exit 2
fi

repo_root="$1"
out_dir="${repo_root}/artifacts/telemetry/ctest-diagnostics-dry-run"
rm -rf "${out_dir}"

"${repo_root}/scripts/diagnostics-dry-run.sh" \
  "${repo_root}/config/server_diagnostics_alert.json" \
  2 \
  "${out_dir}" \
  "active_players_below_min"

summary="${out_dir}/summary.txt"
if [[ ! -f "${summary}" ]]; then
  echo "Missing diagnostics summary: ${summary}" >&2
  exit 1
fi

if ! grep -q '^status=pass$' "${summary}"; then
  echo "Diagnostics dry run failed" >&2
  cat "${summary}"
  exit 1
fi
