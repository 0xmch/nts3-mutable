#!/bin/zsh
# Runs one effect on the NTS-3 over MIDI: selects it into FX 1 (CC 80 value
# $1), turns FX 1 on, touches and sweeps the pad for about three seconds, and
# checks after each step that the device still answers.
#   scripts/devicetest.sh <cc value>
set -u
ctl=$(dirname $0)/../tools/nts3ctl
alive() { $ctl current >/dev/null 2>&1 && $ctl current | grep -q 'current program' && echo yes || echo NO; }
$ctl cc 80 $1 106 127 >/dev/null
fx=$($ctl program | sed -n '3p' | sed 's/^ *//' | cut -c1-48)
echo "cc $1 -> $fx | answering: $(alive)"
$ctl cc 104 127 >/dev/null
for v in 0 32 64 96 127 96 64 32 0 64; do $ctl cc 16 $v 17 $(( 127 - v )) 18 100 >/dev/null; done
$ctl cc 104 0 >/dev/null
echo "   after touch and sweep | answering: $(alive)"
