#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"

APP_BIN="./build/macos-default-relwithdebinfo/Dusk.app/Contents/MacOS/Dusk"
DISC_IMAGE="${1:-./game.ciso}"

if [[ ! -x "$APP_BIN" ]]; then
  echo "Dusk macOS app binary not found at: $APP_BIN" >&2
  echo "Build it with:" >&2
  echo "  cmake --preset macos-default-relwithdebinfo" >&2
  echo "  cmake --build --preset macos-default-relwithdebinfo" >&2
  exit 1
fi

if [[ ! -f "$DISC_IMAGE" ]]; then
  echo "Disc image not found: $DISC_IMAGE" >&2
  echo "Pass a supported disc image path or place a local game.ciso in the repo root." >&2
  exit 1
fi

exec "$APP_BIN" "$DISC_IMAGE"
