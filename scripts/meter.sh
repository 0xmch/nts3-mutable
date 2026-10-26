#!/bin/bash
# Measures a unit's CPU load on the NTS-3 (see units/common/meter.h): builds a
# metered variant with the given parameters, uploads it to user slot 1, selects
# it as FX 1 with the pad held (effects only process while touched) and
# decodes its reports. One run per parameter set. Gaps of about 285 ms that
# match no report are the firmware itself, likely saving the edit to flash.
#
#   scripts/meter.sh <unit> '{{id,value},...}' ['{{...}}' ...]
#
# METER_DEFS adds compile definitions, e.g. -DNTS3_METER_SUSTAINED_ONLY
# (units/common/meter.h).
#
# Slot 1 is left holding the last variant: tools/nts3ctl clear 1 afterwards.
set -euo pipefail
unit=$1
shift
source "$(dirname "$0")/sync.sh"
ctl="$ROOT/tools/nts3ctl"
out=$(mktemp -d)
for params in "$@"; do
  ssh "$HOST" "bash -c 'set -e; cd ~/$REMOTE/units/$unit && printf \"#define NTS3_METER_PARAMS %s\n\" \"$params\" > meter_params.h && make -s clean >/dev/null && make -s EXTRA_DEFS=\"-DNTS3_METER ${METER_DEFS:-}\" 2>&1 | grep -iE \"error\" || true; make -s install >/dev/null'"
  scp -q "$HOST:$REMOTE/units/$unit/$unit.nts3unit" "$out/"
  "$ctl" cc 80 0 >/dev/null
  program=$("$ctl" current | grep 'current program' | grep -oE '[0-9]+$')
  "$ctl" load "$out/$unit.nts3unit" 1 | tail -1 | grep -q COMPLETED || { echo "upload failed"; exit 1; }
  echo "== $unit $params (program $program)"
  # First gap g ms: period (g - 30) * 550000 / 64 cycles. Then the slowest
  # and the average call in turn, (g - 30) / 5 percent of the period. 1 s
  # more once tripped.
  "$ctl" watch s1 15.5 --hold | awk '/reply gap/ { g = $5; n++; t = g > 900; if (t) g -= 1000;
      if (n == 1) printf "  period %.0f cycles (%.0f frames at 550 MHz)%s\n", (g - 30) * 550000 / 64, (g - 30) * 48 / 64, t ? ", TRIPPED" : "";
      else printf "  %s call %.0f%% of the period%s\n", n % 2 ? "average" : "slowest", (g - 30) / 5, t ? ", TRIPPED" : "" }
    /no reply/ { print "  NO REPLY: device not answering, stopping"; exit 1 }' || {
    "$ctl" cc 104 0 >/dev/null 2>&1 || true
    echo "the device stopped answering: no further runs"
    exit 1
  }
  # A run during which the program changed (a knob turned on the device)
  # measured something else.
  now=$("$ctl" current | grep 'current program' | grep -oE '[0-9]+$')
  if [ "$now" != "$program" ]; then
    echo "  INVALID: the device switched to program $now during the run; back to $program"
    "$ctl" cc 80 0 >/dev/null
    "$ctl" pc "$now" >/dev/null
    "$ctl" pc "$program" >/dev/null
  fi
done
rm -rf "$out"
