#!/bin/bash
# Build the web page's WebAssembly units on the build host and serve web/:
#
#   scripts/web.sh              # all units, then http://localhost:8791/
#   scripts/web.sh resonator
#
# web/ is plain static files; the server sends no special headers, the same
# as a blog host would.
set -euo pipefail
if [ $# -gt 0 ]; then
  UNITS=("$@")
else
  UNITS=()
  for c in "$(dirname "$0")"/../units/*/config.mk; do UNITS+=("$(basename "$(dirname "$c")")"); done
fi
PORT=${NTS3_WEB_PORT:-8791}
source "$(dirname "$0")/sync.sh"

mkdir -p "$ROOT/web/units"
for u in "${UNITS[@]}"; do
  ssh "$HOST" "bash -c 'source ~/src/logue-sdk/tools/emsdk/emsdk_env.sh >/dev/null 2>&1; make -s -C ~/$REMOTE/web UNIT=$u'"
  scp -q "$HOST:$REMOTE/web/units/$u.wasm" "$ROOT/web/units/"
done
echo "http://localhost:$PORT/"
if ! lsof -nP -iTCP:"$PORT" -sTCP:LISTEN >/dev/null 2>&1; then
  exec python3 "$ROOT/host/serve.py" --plain "$ROOT/web" "$PORT"
fi
