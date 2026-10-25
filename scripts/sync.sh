#!/bin/bash
# Copy the source tree to the Linux build box (sourced by build.sh, sim.sh and
# web.sh). Build outputs stay behind, so the build box always rebuilds them.
HOST=${NTS3_BUILD_HOST:-pc}
REMOTE=src/nts3-mutable-build
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
COPYFILE_DISABLE=1 tar --no-xattrs -C "$ROOT" --exclude build --exclude dist --exclude sim \
  --exclude '*.nts3unit' --exclude .git --exclude host/build --exclude host/out \
  --exclude host/inputs --exclude web/units -cf - . |
  ssh "$HOST" "bash -c 'rm -rf ~/$REMOTE && mkdir -p ~/$REMOTE && tar -C ~/$REMOTE -xf -'"
