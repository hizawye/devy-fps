#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
preset="${1:-release-vcpkg}"
out_dir="${2:-${repo_root}/artifacts/releases}"

build_dir="${repo_root}/build/presets/${preset}"
server_bin="${build_dir}/server/devy_server"
client_bin="${build_dir}/client/devy_client"
load_client_bin="${build_dir}/server/devy_load_client"

if [[ "${DEVY_SKIP_BUILD:-0}" != "1" ]]; then
  "${repo_root}/scripts/configure.sh" "${preset}"
  "${repo_root}/scripts/build.sh" "${preset}"
fi

for bin in "${server_bin}" "${client_bin}" "${load_client_bin}"; do
  if [[ ! -x "${bin}" ]]; then
    echo "Required binary missing after build: ${bin}" >&2
    exit 1
  fi
done

mkdir -p "${out_dir}"

git_commit="$(git -C "${repo_root}" rev-parse --short=12 HEAD 2>/dev/null || true)"
if [[ -z "${git_commit}" ]]; then
  git_commit="unknown"
fi

package_name="devy-fps-${preset}-${git_commit}"
staging_parent="$(mktemp -d)"
package_dir="${staging_parent}/${package_name}"
mkdir -p \
  "${package_dir}/bin" \
  "${package_dir}/config" \
  "${package_dir}/config/templates" \
  "${package_dir}/docs" \
  "${package_dir}/docs/releases" \
  "${package_dir}/profiles/launch"

cp "${server_bin}" "${package_dir}/bin/"
cp "${client_bin}" "${package_dir}/bin/"
cp "${load_client_bin}" "${package_dir}/bin/"
cp "${repo_root}/README.md" "${package_dir}/README.md"
cp "${repo_root}/docs/changelog.md" "${package_dir}/docs/changelog.md"
cp "${repo_root}/docs/rollback-strategy.md" "${package_dir}/docs/rollback-strategy.md"
cp "${repo_root}/config/"*.json "${package_dir}/config/"
if compgen -G "${repo_root}/config/templates/*.json" >/dev/null; then
  cp "${repo_root}/config/templates/"*.json "${package_dir}/config/templates/"
fi
if compgen -G "${repo_root}/profiles/launch/*.env" >/dev/null; then
  cp "${repo_root}/profiles/launch/"*.env "${package_dir}/profiles/launch/"
fi
if compgen -G "${repo_root}/docs/releases/*.md" >/dev/null; then
  cp "${repo_root}/docs/releases/"*.md "${package_dir}/docs/releases/"
fi

(
  cd "${package_dir}"
  find . -type f ! -name "SHA256SUMS" -print0 | sort -z | xargs -0 sha256sum > SHA256SUMS
)

archive_tar="${out_dir}/${package_name}.tar"
archive_gz="${archive_tar}.gz"

tar \
  --sort=name \
  --mtime="UTC 1970-01-01" \
  --owner=0 \
  --group=0 \
  --numeric-owner \
  -cf "${archive_tar}" \
  -C "${staging_parent}" \
  "${package_name}"

gzip -n -f "${archive_tar}"
sha256sum "${archive_gz}" > "${archive_gz}.sha256"

rm -rf "${staging_parent}"

echo "Packaged artifact: ${archive_gz}"
echo "Artifact hash file: ${archive_gz}.sha256"
