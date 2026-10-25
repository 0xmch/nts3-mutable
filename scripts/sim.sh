#!/bin/bash
# Build a unit for KORG's browser simulator (logue SDK websim) on the Linux box
# and serve it from this machine:
#
#   scripts/sim.sh resonator   # then open http://localhost:8790/resonator/resonator.html
#
# The page runs the unit's Effect class in an AudioWorklet with an XY pad, the
# SDK's sample loops and a test oscillator.
set -euo pipefail
UNIT=${1:-resonator}
PORT=${NTS3_SIM_PORT:-8790}
source "$(dirname "$0")/sync.sh"

ssh "$HOST" "bash -c 'source ~/src/logue-sdk/tools/emsdk/emsdk_env.sh >/dev/null 2>&1; cd ~/$REMOTE/units/$UNIT && make -s wasm 2>&1 | grep -E \"error|Done\"'"
rm -rf "$ROOT/sim/$UNIT" && mkdir -p "$ROOT/sim/$UNIT"
ssh "$HOST" "tar -C ~/$REMOTE/units/$UNIT/sim -cf - ." | tar -C "$ROOT/sim/$UNIT" -xf -
# Index of every unit built so far.
{
  echo '<!doctype html><meta charset="utf-8"><title>nts3-mutable sim</title>'
  echo '<body style="font-family:system-ui;margin:2em"><h1>nts3-mutable</h1><ul>'
  for d in "$ROOT"/sim/*/; do
    u=$(basename "$d")
    [ -f "$d/$u.html" ] && echo "<li><a href=\"$u/$u.html\">$u</a></li>"
  done
  echo '</ul><p>Start audio first. Volume starts at 0.05.</p></body>'
} > "$ROOT/sim/index.html"
echo "http://localhost:$PORT/$UNIT/$UNIT.html"
if ! lsof -nP -iTCP:"$PORT" -sTCP:LISTEN >/dev/null 2>&1; then
  exec python3 "$ROOT/host/serve.py" "$ROOT/sim" "$PORT"
fi
