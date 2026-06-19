#!/usr/bin/env bash
# Launch Solero (debug build) with the linuxbrew Qt6 platform plugins on PATH.
# The app links against /home/linuxbrew/.linuxbrew/opt/qtbase, whose plugins live
# in a non-standard share/qt/plugins layout that Qt won't find on its own.
set -euo pipefail

QT_BASE="/home/linuxbrew/.linuxbrew/opt/qtbase"
export QT_PLUGIN_PATH="${QT_BASE}/share/qt/plugins"
export QT_QPA_PLATFORM_PLUGIN_PATH="${QT_BASE}/share/qt/plugins/platforms"

# Wayland session defaults (harmless if already exported by the session).
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"
export WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-wayland-0}"

BIN="$(dirname "$(readlink -f "$0")")/build/solero"
exec "$BIN" "$@"
