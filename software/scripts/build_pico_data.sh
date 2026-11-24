#!/usr/bin/env bash
# Assemble software/data/ (the Pico's LittleFS image):
#   /            reward display (lm05-gameplay + display-patch/), opened as /?ws=auto
#   /panel/      clinician panel (panel/ build), JS/CSS stored gzipped
#
# Then flash it with:   pio run -e pico -t uploadfs
#
# Display source (local lm05-gameplay clone, default ~/Developer/lm05-gameplay):
#   1. its branch $GAMEPLAY_REF (default centad-ws-bridge) if that exists
#   2. otherwise its current files + display-patch/*.patch (if not already patched)
#   3. no local clone: GitHub main + display-patch/*.patch
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DATA="$ROOT/data"
GAMEPLAY_DIR="${GAMEPLAY_DIR:-$HOME/Developer/lm05-gameplay}"
GAMEPLAY_REF="${GAMEPLAY_REF:-centad-ws-bridge}"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

rm -rf "$DATA"
mkdir -p "$DATA"

apply_patch() {   # $1 = dir holding the display files
  if grep -q "SENSOR BRIDGE" "$1/index.html"; then return; fi
  ( cd "$1" && patch -s -p1 < "$(ls "$ROOT"/display-patch/*.patch | head -1)" )
}

# ---- 1. reward display -------------------------------------------------------
if [[ -d "$GAMEPLAY_DIR/.git" ]] && git -C "$GAMEPLAY_DIR" rev-parse -q --verify "$GAMEPLAY_REF" >/dev/null; then
  echo "display: $GAMEPLAY_DIR @ branch $GAMEPLAY_REF"
  git -C "$GAMEPLAY_DIR" archive "$GAMEPLAY_REF" | tar -x -C "$TMP"
elif [[ -f "$GAMEPLAY_DIR/index.html" ]]; then
  echo "display: $GAMEPLAY_DIR (working copy) + display-patch/"
  cp "$GAMEPLAY_DIR"/index.html "$GAMEPLAY_DIR"/manifest.json "$GAMEPLAY_DIR"/sw.js "$GAMEPLAY_DIR"/README.md "$GAMEPLAY_DIR"/icon-*.png "$TMP"/
  apply_patch "$TMP"
else
  echo "display: github main + display-patch/"
  git clone -q --depth 1 https://github.com/lohhaoyuan/lm05-gameplay.git "$TMP/repo"
  apply_patch "$TMP/repo"
  cp -R "$TMP/repo/." "$TMP"
fi
grep -q "SENSOR BRIDGE" "$TMP/index.html" || { echo "ERROR: display has no WebSocket bridge (patch not applied?)"; exit 1; }
cp "$TMP"/index.html "$TMP"/manifest.json "$TMP"/sw.js "$TMP"/icon-*.png "$DATA"/

# ---- 2. clinician panel ------------------------------------------------------
echo "panel: building"
( cd "$ROOT/panel" && { [[ -d node_modules ]] || npm ci; } && npm run build >/dev/null )
mkdir -p "$DATA/panel"
cp -R "$ROOT/panel/dist/." "$DATA/panel/"
# The Pico serves foo.js.gz with Content-Encoding: gzip when foo.js is absent.
find "$DATA/panel/assets" -type f \( -name '*.js' -o -name '*.css' \) -exec gzip -9 {} \;

# ---- 3. size check (board_build.filesystem_size = 1m) ------------------------
KB=$(du -sk "$DATA" | cut -f1)
echo "data/: ${KB} KB"
find "$DATA" -type f | sed "s#$DATA##" | sort
if (( KB > 900 )); then
  echo "WARNING: close to the 1 MB LittleFS partition; raise board_build.filesystem_size in platformio.ini"
fi
echo "done. Flash with:  pio run -e pico -t uploadfs"
