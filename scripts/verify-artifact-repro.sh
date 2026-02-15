#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
preset="${1:-release-vcpkg}"
out_root="${2:-${repo_root}/artifacts/releases/repro-check}"
skip_build="${3:-0}"

run_a="${out_root}/run-a"
run_b="${out_root}/run-b"

rm -rf "${run_a}" "${run_b}"
mkdir -p "${run_a}" "${run_b}"

if [[ "${skip_build}" == "1" ]]; then
  DEVY_SKIP_BUILD=1 "${repo_root}/scripts/package-artifacts.sh" "${preset}" "${run_a}"
  DEVY_SKIP_BUILD=1 "${repo_root}/scripts/package-artifacts.sh" "${preset}" "${run_b}"
else
  "${repo_root}/scripts/package-artifacts.sh" "${preset}" "${run_a}"
  "${repo_root}/scripts/package-artifacts.sh" "${preset}" "${run_b}"
fi

archive_a="$(find "${run_a}" -maxdepth 1 -type f -name '*.tar.gz' | head -n 1)"
archive_b="$(find "${run_b}" -maxdepth 1 -type f -name '*.tar.gz' | head -n 1)"

if [[ -z "${archive_a}" || -z "${archive_b}" ]]; then
  echo "Could not find packaged archives in ${run_a} or ${run_b}" >&2
  exit 1
fi

hash_a="$(sha256sum "${archive_a}" | awk '{print $1}')"
hash_b="$(sha256sum "${archive_b}" | awk '{print $1}')"

if [[ "${hash_a}" != "${hash_b}" ]]; then
  echo "Artifact reproducibility check failed." >&2
  echo "  ${archive_a}: ${hash_a}" >&2
  echo "  ${archive_b}: ${hash_b}" >&2
  exit 1
fi

echo "Artifact reproducibility check passed."
echo "Hash: ${hash_a}"
