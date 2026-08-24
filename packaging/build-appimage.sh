#!/usr/bin/env bash
# Builds a portable Linux AppImage from an existing (or freshly configured)
# build directory. See PLAN.md's "v1 (MVP) scope" for why AppImage over
# Flatpak/distro packages: portable, no install/root, no sandboxing or
# GPU-passthrough overhead.
#
# Usage: packaging/build-appimage.sh [build-dir]
#
# Downloads linuxdeploy + its Qt plugin (upstream continuous releases) into
# packaging/tools/ on first run and reuses them after that. Requires a Qt6
# qmake on PATH (qmake6 or qmake) so linuxdeploy-plugin-qt can find Qt's
# libs/plugins to bundle.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${1:-${repo_root}/build}"
tools_dir="${repo_root}/packaging/tools"
appdir="${build_dir}/AppDir"

mkdir -p "${tools_dir}"

linuxdeploy="${tools_dir}/linuxdeploy-x86_64.AppImage"
linuxdeploy_qt="${tools_dir}/linuxdeploy-plugin-qt-x86_64.AppImage"

fetch_tool() {
  local url="$1" dest="$2"
  if [[ ! -x "${dest}" ]]; then
    echo "Fetching $(basename "${dest}")..."
    curl -sSL --fail -o "${dest}" "${url}"
    chmod +x "${dest}"
  fi
}

fetch_tool \
  "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage" \
  "${linuxdeploy}"
fetch_tool \
  "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage" \
  "${linuxdeploy_qt}"

if ! command -v qmake6 >/dev/null 2>&1 && ! command -v qmake >/dev/null 2>&1; then
  echo "error: no qmake6/qmake on PATH; linuxdeploy-plugin-qt needs it to locate Qt" >&2
  exit 1
fi
export QMAKE="$(command -v qmake6 || command -v qmake)"

if [[ ! -f "${build_dir}/build.ninja" ]]; then
  cmake --preset default -B "${build_dir}"
fi
cmake --build "${build_dir}"

rm -rf "${appdir}"
cmake --install "${build_dir}" --prefix "${appdir}/usr"

# linuxdeploy and its Qt plugin are themselves AppImages, normally mounted
# via FUSE; CI runners commonly lack FUSE, so extract-and-run instead. The
# env var (rather than the --appimage-extract-and-run CLI flag) is what
# propagates to the plugin AppImage linuxdeploy launches as a subprocess.
export APPIMAGE_EXTRACT_AND_RUN=1

# The bundled `strip` in linuxdeploy's continuous release predates DT_RELR
# (compact relative relocations, now default on current glibc/binutils e.g.
# Arch): it aborts deployment entirely the moment it meets a `.relr.dyn`
# section, which every system lib built by such a toolchain has. Skipping
# strip is a functional no-op (a somewhat larger AppImage, nothing else)
# since the executable itself already carries debug info by design
# (CMAKE_BUILD_TYPE=RelWithDebInfo).
export NO_STRIP=1

cd "${build_dir}"

# Deploying and packaging are split into three steps (rather than one
# `linuxdeploy --plugin qt --output appimage` call) so the Qt plugin step
# can exclude kimg_*.so: on distros where KDE's kimageformats package
# shares Qt's plugins/imageformats directory (e.g. Arch), several of those
# plugins (kimg_jxr.so among them) depend on libraries that aren't actually
# resolvable on a stock install, which otherwise aborts deployment entirely
# over image formats (JPEG-XR, PSD, RAW, ...) this app never reads or
# writes — it only needs Qt's own built-in formats plus giflib for GIF.
"${linuxdeploy}" \
  --appdir "${appdir}" \
  --executable "${appdir}/usr/bin/transparent" \
  --desktop-file "${repo_root}/packaging/transparent.desktop" \
  --icon-file "${repo_root}/resources/icon.png"

"${linuxdeploy_qt}" \
  --appdir "${appdir}" \
  --exclude-library="kimg_*.so"

"${linuxdeploy}" \
  --appdir "${appdir}" \
  --output appimage

echo "AppImage written to ${build_dir}/"
