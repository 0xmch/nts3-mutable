#!/bin/bash
# Build NTS-3 units on the Linux box, where Korg's pinned arm-none-eabi-gcc
# 10.3 lives in ~/src/logue-sdk/tools, and copy the .nts3unit files to dist/.
#
#   scripts/build.sh            # all units
#   scripts/build.sh resonator  # one unit
set -euo pipefail

if [ $# -gt 0 ]; then UNITS=("$@"); else UNITS=($(ls "$(dirname "$0")/../units")); fi
source "$(dirname "$0")/sync.sh"

mkdir -p "$ROOT/dist"
SIZE='~/src/logue-sdk/tools/gcc/gcc-arm-none-eabi-10.3-2021.10/bin/arm-none-eabi-size'
for u in "${UNITS[@]}"; do
  echo "== $u"
  ssh "$HOST" "bash -c 'set -e; cd ~/$REMOTE/units/$u && make -s 2>&1 | grep -vE \"^(Compiling|Linking|Creating|Done|Compiler Options|$)\" | grep -v arm-none-eabi-g || true; make -s install >/dev/null; $SIZE -A build/$u.elf | grep -E \"^\\.(text|data|bss|unit_header|init_array)|Total\"; ls -l $u.nts3unit | cut -d\" \" -f5'"
  scp -q "$HOST:$REMOTE/units/$u/$u.nts3unit" "$ROOT/dist/"
done
ls -l "$ROOT/dist"
