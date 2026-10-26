#!/bin/zsh
# Waits for the NTS-3 to answer, then takes a misbehaving user unit out of
# play: FX 1 to a built-in effect (LPF), the saved program reloaded (discarding
# the edit buffer), and the given user slot cleared.
#   scripts/rescue.sh <program 1-100> <slot to clear>
ctl=$(dirname $0)/../tools/nts3ctl
echo "waiting for the NTS-3..."
until $ctl current 2>/dev/null | grep -q 'current program'; do :; done
echo "NTS-3 answering at $(date +%T)"
$ctl cc 80 0 >/dev/null && echo "FX 1 -> built-in LPF"
$ctl pc $1 | tail -1
$ctl clear $2 | tail -1
$ctl program | sed -n '2,6p'
$ctl probe | grep -E 'slot  1|empty' | cut -c1-70
