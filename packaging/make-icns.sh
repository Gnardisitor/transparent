#!/usr/bin/env bash
# Generates resources/icon.icns from resources/icon.png with sips and
# iconutil. Run on any Mac, including the GitHub Actions macOS runner, before
# the first cmake configure: src/CMakeLists.txt picks the .icns up into the
# app bundle when it exists.
#
# Usage: packaging/make-icns.sh
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
src="${repo_root}/resources/icon.png"
out="${repo_root}/resources/icon.icns"
iconset="$(mktemp -d)/icon.iconset"

mkdir -p "${iconset}"

# macOS wants every size in the iconset; iconutil picks the best per context.
for spec in 16 32 128 256 512; do
  sips -z "${spec}" "${spec}" "${src}" --out "${iconset}/icon_${spec}x${spec}.png" >/dev/null
done
for spec in 32 64 256 512 1024; do
  half=$((spec / 2))
  sips -z "${spec}" "${spec}" "${src}" --out "${iconset}/icon_${half}x${half}@2x.png" >/dev/null
done

iconutil -c icns "${iconset}" -o "${out}"
echo "Wrote ${out}"
