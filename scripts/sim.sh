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
echo "http://localhost:$PORT/$UNIT/$UNIT.html"
if ! lsof -nP -iTCP:"$PORT" -sTCP:LISTEN >/dev/null 2>&1; then
  exec python3 "$ROOT/host/serve.py" "$ROOT/sim" "$PORT"
fi
