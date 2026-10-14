#!/bin/bash
# Copies the web preview into the bfunc.cc repo (../bfunc.cc/midinja); deploy from there.
set -euo pipefail
cd "$(dirname "$0")"
DEST="../../bfunc.cc/midinja"
cp index.html midinja-core.js "$DEST/"
echo "copied to $DEST — now run ./scripts/deploy.sh midinja in bfunc.cc"
