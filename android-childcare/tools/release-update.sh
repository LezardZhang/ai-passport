#!/bin/sh
# Run after increasing the manifest version code and validating the feature change.
# Publishing is an explicit operation; a normal build never contacts the release channel.
set -eu
cd "$(dirname "$0")/.."
./tools/test-host.sh
./tools/build.sh
python3 ./tools/publish-update.py prepare "$@"
python3 ./tools/publish-update.py publish "$@"
