#!/bin/bash
# Build NTS-3 units on the Linux box, where Korg's pinned arm-none-eabi-gcc
# 10.3 lives in ~/src/logue-sdk/tools, and copy the .nts3unit files to dist/.
#
#   scripts/build.sh            # all units
#   scripts/build.sh resonator  # one unit
set -euo pipefail

if [ $# -gt 0 ]; then
  UNITS=("$@")
else
  UNITS=()
  for c in "$(dirname "$0")"/../units/*/config.mk; do UNITS+=("$(basename "$(dirname "$c")")"); done
fi
source "$(dirname "$0")/sync.sh"

mkdir -p "$ROOT/dist"
BIN='~/src/logue-sdk/tools/gcc/gcc-arm-none-eabi-10.3-2021.10/bin/arm-none-eabi'
# The NTS-3 refuses a unit file over 32768 bytes (USER DATA SIZE ERROR) and one
# whose loaded segments, code + data + bss, add up to more (USER LOAD SIZE
# ERROR).
LIMIT=32768
for u in "${UNITS[@]}"; do
  echo "== $u"
  out=$(ssh "$HOST" "bash -c 'set -e; cd ~/$REMOTE/units/$u && make -s 2>&1 | grep -vE \"^(Compiling|Linking|Creating|Done|Compiler Options|$)\" | grep -v arm-none-eabi-g || true; make -s install >/dev/null; $BIN-size -A build/$u.elf | grep -E \"^\\.(text|data|bss|unit_header|init_array)|Total\"; $BIN-readelf -lW build/$u.elf | grep LOAD'")
  echo "$out" | grep -v LOAD
  load=0
  for m in $(echo "$out" | awk '/LOAD/ {print $6}'); do load=$((load + m)); done
  scp -q "$HOST:$REMOTE/units/$u/$u.nts3unit" "$ROOT/dist/"
  file=$(wc -c < "$ROOT/dist/$u.nts3unit" | tr -d ' ')
  echo "file $file bytes, loaded $load bytes (limit $LIMIT each)"
  if [ "$file" -gt $LIMIT ] || [ "$load" -gt $LIMIT ] || [ "$load" -eq 0 ]; then
    echo "$u: too big for the NTS-3" >&2
    rm -f "$ROOT/dist/$u.nts3unit"
    exit 1
  fi
done
cp "$ROOT/LICENSE" "$ROOT/THIRD_PARTY_NOTICES.md" "$ROOT/dist/"
ls -l "$ROOT/dist"
