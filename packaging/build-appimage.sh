#!/usr/bin/env bash
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

export APPIMAGE_EXTRACT_AND_RUN=1
export NO_STRIP=1

cd "${build_dir}"

"${linuxdeploy}" \
  --appdir "${appdir}" \
  --executable "${appdir}/usr/bin/transparent" \
  --desktop-file "${repo_root}/packaging/transparent.desktop" \
  --icon-file "${repo_root}/resources/icon.png" \
  --custom-apprun "${repo_root}/packaging/AppRun.sh"

"${linuxdeploy_qt}" \
  --appdir "${appdir}" \
  --exclude-library="kimg_*.so"

qt_plugins_dir="$("${QMAKE}" -query QT_INSTALL_PLUGINS)"
mapfile -t style_plugins < <(find "${qt_plugins_dir}/styles" -maxdepth 1 -iname '*.so' 2>/dev/null | sort)
kde_platformtheme="$(find "${qt_plugins_dir}/platformthemes" -maxdepth 1 -iname '*kdeplasma*.so' 2>/dev/null | sort | head -n1)"
kicon_engine="$(find "${qt_plugins_dir}" -path '*iconengines*' -iname 'kiconengineplugin*.so' 2>/dev/null | sort | head -n1)"

if [[ "${#style_plugins[@]}" -gt 0 && -n "${kde_platformtheme}" ]]; then
  echo "Bundling KDE Plasma theme integration: ${style_plugins[*]}, ${kde_platformtheme}"
  mkdir -p "${appdir}/usr/plugins/styles" "${appdir}/usr/plugins/platformthemes"
  bundle_args=(--library "${appdir}/usr/plugins/platformthemes/$(basename "${kde_platformtheme}")")
  cp "${kde_platformtheme}" "${appdir}/usr/plugins/platformthemes/"
  for style_plugin in "${style_plugins[@]}"; do
    cp "${style_plugin}" "${appdir}/usr/plugins/styles/"
    bundle_args+=(--library "${appdir}/usr/plugins/styles/$(basename "${style_plugin}")")
  done
  if [[ -n "${kicon_engine}" ]]; then
    icon_engine_reldir="$(dirname "${kicon_engine#${qt_plugins_dir}/}")"
    mkdir -p "${appdir}/usr/plugins/${icon_engine_reldir}"
    cp "${kicon_engine}" "${appdir}/usr/plugins/${icon_engine_reldir}/"
    bundle_args+=(--library "${appdir}/usr/plugins/${icon_engine_reldir}/$(basename "${kicon_engine}")")
  fi
  if [[ -d /usr/share/color-schemes ]]; then
    mkdir -p "${appdir}/usr/share/color-schemes"
    cp /usr/share/color-schemes/Breeze*.colors "${appdir}/usr/share/color-schemes/" 2>/dev/null || true
  fi
  "${linuxdeploy}" --appdir "${appdir}" "${bundle_args[@]}"
else
  echo "No KDE Plasma theme integration found on this build machine (looked in ${qt_plugins_dir}) — skipping, AppImage will use Qt's default look on KDE too"
fi

"${linuxdeploy}" \
  --appdir "${appdir}" \
  --output appimage

echo "AppImage written to ${build_dir}/"
