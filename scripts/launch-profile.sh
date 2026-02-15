#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

usage() {
  cat <<USAGE
Usage: $0 [profile] [server|client|both] [preset-or-bin-root]

Examples:
  $0 local-dev server release-vcpkg
  $0 canary both /opt/devy-fps
USAGE
}

profile_name="${1:-local-dev}"
mode="${2:-both}"
preset_or_root="${3:-release-vcpkg}"

case "${mode}" in
  server|client|both)
    ;;
  -h|--help)
    usage
    exit 0
    ;;
  *)
    echo "Invalid mode: ${mode}" >&2
    usage >&2
    exit 2
    ;;
esac

resolve_bin_root() {
  local selector="$1"
  if [[ -d "${selector}" ]]; then
    echo "${selector}"
    return
  fi

  local preset_candidate="${repo_root}/build/presets/${selector}"
  if [[ -d "${preset_candidate}" ]]; then
    echo "${preset_candidate}"
    return
  fi

  if [[ -x "${repo_root}/bin/devy_server" ]]; then
    # Package layout fallback.
    echo "${repo_root}"
    return
  fi

  echo "${preset_candidate}"
}

find_binary() {
  local requested="${1}"
  shift

  if [[ -n "${requested}" ]]; then
    echo "${requested}"
    return
  fi

  local candidate
  for candidate in "$@"; do
    if [[ -x "${candidate}" ]]; then
      echo "${candidate}"
      return
    fi
  done

  echo ""
}

resolve_config_path() {
  local raw="$1"
  if [[ -f "${raw}" ]]; then
    echo "${raw}"
    return
  fi

  if [[ -f "${repo_root}/${raw}" ]]; then
    echo "${repo_root}/${raw}"
    return
  fi

  echo "${raw}"
}

profile_path="${repo_root}/profiles/launch/${profile_name}.env"
if [[ ! -f "${profile_path}" ]]; then
  echo "Launch profile not found: ${profile_path}" >&2
  exit 1
fi

# shellcheck disable=SC1090
source "${profile_path}"

: "${SERVER_CONFIG:?Profile must define SERVER_CONFIG}"
CLIENT_CONFIG="${CLIENT_CONFIG:-${SERVER_CONFIG}}"
LAUNCH_DELAY_SECONDS="${LAUNCH_DELAY_SECONDS:-0.3}"

server_args=()
if declare -p SERVER_ARGS >/dev/null 2>&1; then
  server_args=("${SERVER_ARGS[@]}")
fi

client_args=()
if declare -p CLIENT_ARGS >/dev/null 2>&1; then
  client_args=("${CLIENT_ARGS[@]}")
fi

bin_root="$(resolve_bin_root "${preset_or_root}")"

server_bin="$(find_binary "${DEVY_SERVER_BIN:-}" \
  "${bin_root}/server/devy_server" \
  "${bin_root}/bin/devy_server" \
  "${repo_root}/build/presets/debug/server/devy_server" \
  "${repo_root}/build/presets/debug-vcpkg/server/devy_server")"

client_bin="$(find_binary "${DEVY_CLIENT_BIN:-}" \
  "${bin_root}/client/devy_client" \
  "${bin_root}/bin/devy_client" \
  "${repo_root}/build/presets/debug/client/devy_client" \
  "${repo_root}/build/presets/debug-vcpkg/client/devy_client")"

if [[ "${mode}" == "server" || "${mode}" == "both" ]]; then
  if [[ -z "${server_bin}" ]]; then
    echo "Could not locate devy_server binary under '${bin_root}' or known build presets." >&2
    exit 1
  fi
fi

if [[ "${mode}" == "client" || "${mode}" == "both" ]]; then
  if [[ -z "${client_bin}" ]]; then
    echo "Could not locate devy_client binary under '${bin_root}' or known build presets." >&2
    exit 1
  fi
fi

server_config="$(resolve_config_path "${SERVER_CONFIG}")"
client_config="$(resolve_config_path "${CLIENT_CONFIG}")"

if [[ "${mode}" == "server" ]]; then
  echo "Launching server profile '${profile_name}' using ${server_bin}" >&2
  exec "${server_bin}" "${server_config}" "${server_args[@]}"
fi

if [[ "${mode}" == "client" ]]; then
  echo "Launching client profile '${profile_name}' using ${client_bin}" >&2
  exec "${client_bin}" "${client_config}" "${client_args[@]}"
fi

server_pid=""
cleanup() {
  if [[ -n "${server_pid}" ]] && kill -0 "${server_pid}" >/dev/null 2>&1; then
    kill "${server_pid}" >/dev/null 2>&1 || true
    wait "${server_pid}" >/dev/null 2>&1 || true
  fi
}
trap cleanup EXIT

echo "Launching server profile '${profile_name}' using ${server_bin}" >&2
"${server_bin}" "${server_config}" "${server_args[@]}" &
server_pid="$!"

sleep "${LAUNCH_DELAY_SECONDS}"

echo "Launching client profile '${profile_name}' using ${client_bin}" >&2
"${client_bin}" "${client_config}" "${client_args[@]}"
