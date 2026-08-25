#!/usr/bin/env bash
set -euo pipefail

here="$(cd "$(dirname "$(readlink -f "${0}")")" && pwd)"

export LD_LIBRARY_PATH="${here}/usr/lib${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
export QT_PLUGIN_PATH="${here}/usr/plugins${QT_PLUGIN_PATH:+:${QT_PLUGIN_PATH}}"
export XDG_DATA_DIRS="${here}/usr/share:${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"

if [[ -z "${QT_QPA_PLATFORMTHEME+x}" && "${XDG_CURRENT_DESKTOP:-}" == *KDE* ]]; then
  export QT_QPA_PLATFORMTHEME="kde"
fi

exec "${here}/usr/bin/transparent" "$@"
