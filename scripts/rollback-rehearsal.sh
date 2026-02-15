#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

preset="${1:-debug-vcpkg}"
out_dir="${2:-${repo_root}/artifacts/releases/rollback-rehearsal/$(date +%Y%m%d-%H%M%S)}"
clients="${3:-4}"
phase_seconds="${4:-3}"

baseline_pkg_dir="${out_dir}/baseline-package"
candidate_pkg_dir="${out_dir}/candidate-package"
baseline_extract_dir="${out_dir}/baseline-extract"
candidate_extract_dir="${out_dir}/candidate-extract"
summary_log="${out_dir}/summary.txt"

mkdir -p "${baseline_pkg_dir}" "${candidate_pkg_dir}" "${baseline_extract_dir}" "${candidate_extract_dir}"

if [[ "${DEVY_SKIP_BUILD:-0}" == "1" ]]; then
  DEVY_SKIP_BUILD=1 "${repo_root}/scripts/package-artifacts.sh" "${preset}" "${baseline_pkg_dir}"
  DEVY_SKIP_BUILD=1 "${repo_root}/scripts/package-artifacts.sh" "${preset}" "${candidate_pkg_dir}"
else
  "${repo_root}/scripts/package-artifacts.sh" "${preset}" "${baseline_pkg_dir}"
  DEVY_SKIP_BUILD=1 "${repo_root}/scripts/package-artifacts.sh" "${preset}" "${candidate_pkg_dir}"
fi

find_archive() {
  local dir="$1"
  find "${dir}" -maxdepth 1 -type f -name '*.tar.gz' | head -n 1
}

extract_archive() {
  local archive="$1"
  local dest="$2"
  tar -xzf "${archive}" -C "${dest}"
  find "${dest}" -mindepth 1 -maxdepth 1 -type d | head -n 1
}

baseline_archive="$(find_archive "${baseline_pkg_dir}")"
candidate_archive="$(find_archive "${candidate_pkg_dir}")"
if [[ -z "${baseline_archive}" || -z "${candidate_archive}" ]]; then
  echo "Missing baseline or candidate package archive" >&2
  exit 1
fi

baseline_root="$(extract_archive "${baseline_archive}" "${baseline_extract_dir}")"
candidate_root="$(extract_archive "${candidate_archive}" "${candidate_extract_dir}")"
if [[ -z "${baseline_root}" || -z "${candidate_root}" ]]; then
  echo "Could not determine extracted package roots" >&2
  exit 1
fi

run_phase() {
  local label="$1"
  local package_root="$2"

  local server_log="${out_dir}/server-${label}.log"
  local load_log="${out_dir}/load-client-${label}.log"
  local result_file="${out_dir}/${label}.result"

  local server_bin="${package_root}/bin/devy_server"
  local load_client_bin="${package_root}/bin/devy_load_client"
  local config_path="${package_root}/config/server_test.json"
  local port
  port="$(grep -E '"port"\s*:\s*[0-9]+' "${config_path}" | head -n1 | sed -E 's/[^0-9]*([0-9]+).*/\1/')"
  if [[ -z "${port}" ]]; then
    port="17777"
  fi

  local server_smoke_seconds=$((phase_seconds + 4))
  local server_pid

  (
    cd "${package_root}"
    ./bin/devy_server config/server_test.json --smoke-seconds "${server_smoke_seconds}"
  ) >"${server_log}" 2>&1 &
  server_pid="$!"

  sleep 0.3
  set +e
  (
    cd "${package_root}"
    ./bin/devy_load_client --host 127.0.0.1 --port "${port}" --clients "${clients}" --seconds "${phase_seconds}"
  ) >"${load_log}" 2>&1
  local load_status=$?
  set -e

  set +e
  wait "${server_pid}"
  local server_status=$?
  set -e

  local joined_total
  joined_total="$(grep -o 'joined=[0-9]\+' "${load_log}" | tail -n1 | cut -d= -f2 || true)"
  joined_total="${joined_total:-0}"

  echo "${server_status};${load_status};${joined_total};${port}" >"${result_file}"
}

run_phase "candidate" "${candidate_root}"
run_phase "rollback" "${baseline_root}"

IFS=';' read -r candidate_server_status candidate_load_status candidate_joined candidate_port <"${out_dir}/candidate.result"
IFS=';' read -r rollback_server_status rollback_load_status rollback_joined rollback_port <"${out_dir}/rollback.result"

pass=1
if (( candidate_server_status != 0 )) || (( candidate_load_status != 0 )) || (( candidate_joined <= 0 )); then
  pass=0
fi
if (( rollback_server_status != 0 )) || (( rollback_load_status != 0 )) || (( rollback_joined <= 0 )); then
  pass=0
fi

{
  echo "preset=${preset}"
  echo "baseline_archive=${baseline_archive}"
  echo "candidate_archive=${candidate_archive}"
  echo "clients=${clients}"
  echo "phase_seconds=${phase_seconds}"
  echo "candidate_port=${candidate_port}"
  echo "candidate_server_status=${candidate_server_status}"
  echo "candidate_load_status=${candidate_load_status}"
  echo "candidate_joined=${candidate_joined}"
  echo "rollback_port=${rollback_port}"
  echo "rollback_server_status=${rollback_server_status}"
  echo "rollback_load_status=${rollback_load_status}"
  echo "rollback_joined=${rollback_joined}"
  echo "status=$([[ "${pass}" -eq 1 ]] && echo pass || echo fail)"
  echo
  echo "candidate_load_client_output:"
  sed -n '1,12p' "${out_dir}/load-client-candidate.log"
  echo
  echo "rollback_load_client_output:"
  sed -n '1,12p' "${out_dir}/load-client-rollback.log"
} >"${summary_log}"

if (( pass == 1 )); then
  echo "Rollback rehearsal passed. Artifacts: ${out_dir}"
  exit 0
fi

echo "Rollback rehearsal failed. See ${summary_log}" >&2
exit 1
