#!/usr/bin/env bash
# fetch_libdragon.sh -- checks out libdragon/ at the commit pinned in tools/libdragon.pin,
# the source of the host tools built by tools/vendor/bin/build-tools.sh and of the SDK used by CI.
set -euo pipefail
cd "$(dirname "$0")/.."
PIN="$(tr -d ' \r\n' < tools/libdragon.pin)"
if [ ! -d libdragon/.git ]; then
    git init -q libdragon
    git -C libdragon remote add origin https://github.com/n64brew/libdragon.git
fi
git -C libdragon fetch -q --depth 1 origin "$PIN"
git -C libdragon checkout -q FETCH_HEAD
echo "libdragon at $PIN"
