#!/usr/bin/env bash
set -euo pipefail

if [[ "$#" -lt 1 ]]; then
  echo "usage: $0 <repo-root>" >&2
  exit 2
fi

repo_root="$1"
out_dir="${repo_root}/artifacts/releases/ctest-beta-gate"
version_tag="v0.3.0-beta-ctest"
temp_config=""

if [[ -n "${DEVY_TEST_CONFIG_PATH:-}" ]]; then
  config_path="${DEVY_TEST_CONFIG_PATH}"
else
  mkdir -p "${repo_root}/artifacts/tmp"
  temp_config="${repo_root}/artifacts/tmp/server_test_beta_gate_port18777.json"
  sed -E 's/"port":[[:space:]]*[0-9]+/"port": 18777/' \
    "${repo_root}/config/server_test.json" >"${temp_config}"
  config_path="${temp_config}"
fi

cleanup() {
  rm -f "${repo_root}/docs/releases/${version_tag}-notes.md"
  if [[ -n "${temp_config}" ]]; then
    rm -f "${temp_config}"
  fi
}
trap cleanup EXIT

rm -rf "${out_dir}"

from_ref="$(git -C "${repo_root}" describe --tags --abbrev=0 2>/dev/null || true)"
if [[ -z "${from_ref}" ]]; then
  from_ref="HEAD"
fi

DEVY_SKIP_BUILD=1 "${repo_root}/scripts/beta-release-gate.sh" \
  "${version_tag}" \
  "${config_path}" \
  debug-vcpkg \
  6 \
  4 \
  1 \
  "${out_dir}" \
  0 \
  "${from_ref}"

summary="${out_dir}/summary.txt"
if [[ ! -f "${summary}" ]]; then
  echo "Missing beta release-gate summary: ${summary}" >&2
  exit 1
fi

if ! grep -q '^status=pass$' "${summary}"; then
  echo "Beta release gate failed" >&2
  cat "${summary}"
  exit 1
fi

if ! grep -q '^acceptance_status=pass$' "${summary}"; then
  echo "Beta release gate acceptance did not pass" >&2
  cat "${summary}"
  exit 1
fi

if ! grep -q '^endurance_status=skipped$' "${summary}"; then
  echo "Beta release gate should skip endurance in this test" >&2
  cat "${summary}"
  exit 1
fi

if ! grep -q '^release_notes_status=pass$' "${summary}"; then
  echo "Beta release gate did not generate release notes" >&2
  cat "${summary}"
  exit 1
fi

if ! grep -q '^missing_required_docs=0$' "${summary}"; then
  echo "Beta release gate required-doc check failed" >&2
  cat "${summary}"
  exit 1
fi

notes_file="$(awk -F= '/^release_notes_file=/ { print $2 }' "${summary}")"
if [[ -z "${notes_file}" || ! -f "${notes_file}" ]]; then
  echo "Missing release notes file from beta gate" >&2
  cat "${summary}"
  exit 1
fi
