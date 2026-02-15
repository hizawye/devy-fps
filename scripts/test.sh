#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if [[ -z "${VCPKG_ROOT:-}" ]]; then
  for candidate in "${repo_root}/vcpkg" "${repo_root}/../vcpkg"; do
    if [[ -f "${candidate}/scripts/buildsystems/vcpkg.cmake" ]]; then
      export VCPKG_ROOT="${candidate}"
      break
    fi
  done
fi

preset="${1:-debug}"

if [[ "${preset}" != *"-vcpkg" && -n "${VCPKG_ROOT:-}" ]]; then
  vcpkg_preset="${preset}-vcpkg"
  if cmake --list-presets=test | grep -q "\"${vcpkg_preset}\""; then
    preset="${vcpkg_preset}"
  fi
fi

if [[ "${preset}" == *"-vcpkg" && -z "${VCPKG_ROOT:-}" ]]; then
  echo "Preset '${preset}' requires VCPKG_ROOT (or a local ./vcpkg or ../vcpkg checkout)." >&2
  exit 1
fi

ctest --preset "${preset}"
