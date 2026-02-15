#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
preset="${1:-debug-vcpkg}"
build_dir="${repo_root}/build/presets/${preset}"
cache_path="${build_dir}/CMakeCache.txt"
tmp_dir="$(mktemp -d)"
trap 'rm -rf "${tmp_dir}"' EXIT

snapshot_cache_keys() {
  local cache_file="$1"
  local out_file="$2"
  awk -F= '
    /^(BUILD_TESTING|CMAKE_BUILD_TYPE|CMAKE_CXX_COMPILER|CMAKE_TOOLCHAIN_FILE|DEVY_ENABLE_CLANG_TIDY|DEVY_ENABLE_IMGUI|DEVY_WARNINGS_AS_ERRORS):/ {
      print $1 "=" $2
    }
  ' "${cache_file}" | LC_ALL=C sort > "${out_file}"
}

"${repo_root}/scripts/configure.sh" "${preset}"
if [[ ! -f "${cache_path}" ]]; then
  echo "Missing CMake cache: ${cache_path}" >&2
  exit 1
fi
snapshot_cache_keys "${cache_path}" "${tmp_dir}/cache-1.txt"

"${repo_root}/scripts/configure.sh" "${preset}"
if [[ ! -f "${cache_path}" ]]; then
  echo "Missing CMake cache after reconfigure: ${cache_path}" >&2
  exit 1
fi
snapshot_cache_keys "${cache_path}" "${tmp_dir}/cache-2.txt"

if ! diff -u "${tmp_dir}/cache-1.txt" "${tmp_dir}/cache-2.txt"; then
  echo "CMake cache key drift detected across identical configure runs." >&2
  exit 1
fi

cmake --build --preset "${preset}" > "${tmp_dir}/build-1.log"
cmake --build --preset "${preset}" > "${tmp_dir}/build-2.log"

if grep -Eq "Building CXX object|Linking CXX executable|Linking CXX static library|Generating" "${tmp_dir}/build-2.log"; then
  echo "Second build performed extra work; cache/no-op expectation violated." >&2
  exit 1
fi

echo "Cache correctness checks passed for preset ${preset}."
