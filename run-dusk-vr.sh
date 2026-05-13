#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"

ROOT="$PWD"
DUSK_EXE="$ROOT/build/linux-clang-relwithdebinfo-openxr/dusk"
DISC="${1:-$ROOT/game.ciso}"
STEAMVR_RUNTIME="$HOME/.local/share/Steam/steamapps/common/SteamVR/steamxr_linux.json"

if [[ ! -x "$DUSK_EXE" ]]; then
    echo "Dusk OpenXR executable not found: $DUSK_EXE"
    echo "Build it with:"
    echo "  cmake --build --preset linux-clang-relwithdebinfo-openxr --target dusk"
    exit 1
fi

if [[ ! -f "$DISC" ]]; then
    echo "Game disc image not found: $DISC"
    exit 1
fi

if [[ -z "${XR_RUNTIME_JSON:-}" ]]; then
    if [[ -f "$STEAMVR_RUNTIME" ]]; then
        export XR_RUNTIME_JSON="$STEAMVR_RUNTIME"
    fi
fi

echo "Launching Dusk VR with Vulkan/OpenXR required mode."
if [[ -n "${XR_RUNTIME_JSON:-}" ]]; then
    echo "XR_RUNTIME_JSON=$XR_RUNTIME_JSON"
fi

exec "$DUSK_EXE" --backend vulkan --cvar backend.xrMode=2 "$DISC" "${@:2}"
