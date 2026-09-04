#!/usr/bin/env bash
# Builds the macOS app bundle and DMG (ARM/Apple Silicon only):
#   build/Transparent.app                       - runnable bundle
#   build/dist/Transparent-<version>-macos-arm64.dmg
#
# Ad-hoc signed (no Apple Developer account). First launch needs right-click
# -> Open, or: xattr -d com.apple.quarantine /Applications/Transparent.app
#
# Vulkan reaches the GPU through MoltenVK (Vulkan over Metal). The loader and
# headers come from vcpkg, libMoltenVK.dylib from Homebrew. The loader dylib
# goes into Contents/Frameworks and the ICD manifest into
# Contents/Resources/vulkan/icd.d, which the macOS Vulkan loader searches
# before any system location.
#
# Usage:
#   ./packaging/build-macos.sh
# Env overrides:
#   QT_BIN       - dir containing macdeployqt (default $HOME/qt/6.8.3/macos/bin)
#   MOLTENVK_LIB - dir containing libMoltenVK.dylib (default $(brew --prefix)/lib)
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${repo_root}/build"
dist_dir="${build_dir}/dist"
app="${build_dir}/Transparent.app"

qt_bin="${QT_BIN:-$HOME/qt/6.8.3/macos/bin}"
moltenvk_lib="${MOLTENVK_LIB:-$(brew --prefix)/lib}"
arch="arm64"

# Icon: generated on the fly on macOS (sips/iconutil) if not committed yet.
if [[ ! -f "${repo_root}/resources/icon.icns" ]]; then
  "${repo_root}/packaging/make-icns.sh"
fi

if [[ ! -d "${build_dir}" || ! -f "${build_dir}/build.ninja" ]]; then
  cmake --preset macos
fi
cmake --build "${build_dir}"

raw_bundle="${build_dir}/transparent.app"
if [[ ! -d "${raw_bundle}" ]]; then
  echo "error: ${raw_bundle} not found - did the build produce an app bundle?" >&2
  exit 1
fi

macdeployqt="${qt_bin}/macdeployqt"
if [[ ! -x "${macdeployqt}" ]]; then
  echo "error: macdeployqt not found at ${macdeployqt} (set QT_BIN)" >&2
  exit 1
fi

# Note: no "rm -rf ${app}" before macdeployqt. The macOS volume is usually
# case-insensitive, so Transparent.app and transparent.app are the same
# directory there and the rm would delete the bundle we just built. mv below
# renames it case-only; a stale bundle from an earlier run cannot coexist with
# the raw one on the same volume.
"${macdeployqt}" "${raw_bundle}" -verbose=1
mv "${raw_bundle}" "${app}"

# Bundle the Vulkan loader next to the Qt frameworks and point the
# executable's reference at it. vcpkg builds the loader into
# build/vcpkg_installed/<triplet>/lib.
vcpkg_triplet="${arch}-osx"
vulkan_loader="$(find "${build_dir}/vcpkg_installed/${vcpkg_triplet}/lib" -maxdepth 1 -name 'libvulkan.1*.dylib' | sort | head -n1)"
if [[ -z "${vulkan_loader}" ]]; then
  echo "error: libvulkan loader not found under build/vcpkg_installed/${vcpkg_triplet}/lib" >&2
  exit 1
fi

mkdir -p "${app}/Contents/Frameworks"
cp "${vulkan_loader}" "${app}/Contents/Frameworks/libvulkan.1.dylib"

# Rewrite the executable's reference to the loader (whatever absolute or
# @rpath name the build recorded) to the bundled copy.
exe="${app}/Contents/MacOS/transparent"
recorded="$(otool -L "${exe}" | grep -o 'libvulkan[^ ]*dylib' | head -n1)"
if [[ -n "${recorded}" ]]; then
  install_name_tool -change "${recorded}" "@rpath/libvulkan.1.dylib" "${exe}"
fi
# macdeployqt normally adds this rpath already; make sure it exists.
if ! otool -l "${exe}" | grep -q "@executable_path/../Frameworks"; then
  install_name_tool -add_rpath "@executable_path/../Frameworks" "${exe}"
fi

# ICD manifest. The loader searches the bundle's Resources/vulkan/icd.d
# first. MoltenVK itself comes from Homebrew and ships ad-hoc signed.
moltenvk_dylib="${moltenvk_lib}/libMoltenVK.dylib"
if [[ ! -f "${moltenvk_dylib}" ]]; then
  echo "error: ${moltenvk_dylib} not found (brew install molten-vk, or set MOLTENVK_LIB)" >&2
  exit 1
fi
cp "${moltenvk_dylib}" "${app}/Contents/Frameworks/"
mkdir -p "${app}/Contents/Resources/vulkan/icd.d"
cat > "${app}/Contents/Resources/vulkan/icd.d/MoltenVK_icd.json" <<'EOF'
{
  "file_format_version": "1.0.0",
  "ICD": {
    "library_path": "@executable_path/../Frameworks/libMoltenVK.dylib",
    "api_version": "1.3.0"
  }
}
EOF

# Ad-hoc signature; re-signs after the dylib changes above. Without it the
# modified binary is rejected on launch.
codesign --force --deep --sign - "${app}"

# Default models next to the executable: bundledModelsDefaultsDir() in
# src/main.cpp resolves <Contents/MacOS>/../share/transparent/models.
models_dir="${app}/Contents/share/transparent/models"
mkdir -p "${models_dir}"
cp "${build_dir}"/models/*.gguf "${models_dir}/"

version="${APPIMAGE_VERSION:-$(git -C "${repo_root}" describe --tags --always 2>/dev/null || true)}"
version="${version#v}"

mkdir -p "${dist_dir}"
dmg="${dist_dir}/Transparent-${version}-macos-${arch}.dmg"
rm -f "${dmg}"
hdiutil create -volname "Transparent" -srcfolder "${app}" -ov -format UDZO "${dmg}"

echo "Bundle: ${app}"
echo "DMG:    ${dmg}"
