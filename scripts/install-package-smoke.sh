#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

preset="${1:-debug-vcpkg}"
out_dir="${2:-${repo_root}/artifacts/releases/install-smoke/$(date +%Y%m%d-%H%M%S)}"
smoke_seconds="${3:-4}"
clients="${4:-3}"
load_seconds="${5:-3}"

package_dir="${out_dir}/package"
extract_dir="${out_dir}/extract"
summary_log="${out_dir}/summary.txt"
server_log="${out_dir}/server.log"
load_log="${out_dir}/load-client.log"

mkdir -p "${package_dir}" "${extract_dir}"

if [[ "${DEVY_SKIP_BUILD:-0}" == "1" ]]; then
  DEVY_SKIP_BUILD=1 "${repo_root}/scripts/package-artifacts.sh" "${preset}" "${package_dir}"
else
  "${repo_root}/scripts/package-artifacts.sh" "${preset}" "${package_dir}"
fi

archive="$(find "${package_dir}" -maxdepth 1 -type f -name '*.tar.gz' | head -n 1)"
if [[ -z "${archive}" ]]; then
  echo "No package archive created in ${package_dir}" >&2
  exit 1
fi

tar -xzf "${archive}" -C "${extract_dir}"
package_root="$(find "${extract_dir}" -mindepth 1 -maxdepth 1 -type d | head -n 1)"
if [[ -z "${package_root}" ]]; then
  echo "Could not determine extracted package root" >&2
  exit 1
fi

server_bin="${package_root}/bin/devy_server"
load_client_bin="${package_root}/bin/devy_load_client"
config_path="${package_root}/config/server_test.json"

for required in "${server_bin}" "${load_client_bin}" "${config_path}"; do
  if [[ ! -e "${required}" ]]; then
    echo "Missing required extracted file: ${required}" >&2
    exit 1
  fi
done

port="$(grep -E '"port"\s*:\s*[0-9]+' "${config_path}" | head -n1 | sed -E 's/[^0-9]*([0-9]+).*/\1/')"
if [[ -z "${port}" ]]; then
  port="17777"
fi

server_pid=""
cleanup() {
  if [[ -n "${server_pid}" ]] && kill -0 "${server_pid}" >/dev/null 2>&1; then
    kill "${server_pid}" >/dev/null 2>&1 || true
    wait "${server_pid}" >/dev/null 2>&1 || true
  fi
}
trap cleanup EXIT

server_smoke_seconds=$((smoke_seconds + 3))
(
  cd "${package_root}"
  ./bin/devy_server config/server_test.json --smoke-seconds "${server_smoke_seconds}"
) >"${server_log}" 2>&1 &
server_pid="$!"

sleep 0.3
set +e
(
  cd "${package_root}"
  ./bin/devy_load_client --host 127.0.0.1 --port "${port}" --clients "${clients}" --seconds "${load_seconds}"
) >"${load_log}" 2>&1
load_status=$?
set -e

set +e
wait "${server_pid}"
server_status=$?
set -e
server_pid=""

joined_total="$(grep -o 'joined=[0-9]\+' "${load_log}" | tail -n1 | cut -d= -f2 || true)"
joined_total="${joined_total:-0}"

pass=1
if (( server_status != 0 )) || (( load_status != 0 )) || (( joined_total <= 0 )); then
  pass=0
fi

{
  echo "preset=${preset}"
  echo "archive=${archive}"
  echo "package_root=${package_root}"
  echo "port=${port}"
  echo "smoke_seconds=${smoke_seconds}"
  echo "clients=${clients}"
  echo "load_seconds=${load_seconds}"
  echo "server_status=${server_status}"
  echo "load_status=${load_status}"
  echo "joined_total=${joined_total}"
  echo "status=$([[ "${pass}" -eq 1 ]] && echo pass || echo fail)"
  echo
  echo "load_client_output:"
  sed -n '1,12p' "${load_log}"
} >"${summary_log}"

if (( pass == 1 )); then
  echo "Install-package smoke check passed. Artifacts: ${out_dir}"
  exit 0
fi

echo "Install-package smoke check failed. See ${summary_log}" >&2
exit 1
