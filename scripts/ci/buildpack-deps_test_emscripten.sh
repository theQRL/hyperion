#!/usr/bin/env bash
# Keep image validation on the same build entry point as CI.
set -euo pipefail
exec bash "$(dirname "$0")/build_emscripten.sh" "$@"
