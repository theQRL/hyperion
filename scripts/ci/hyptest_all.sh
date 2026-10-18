#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)

# The first pass includes SMT and option-independent tests. The second pass
# exercises the optimizer without repeating the SMT suite.
status=0
OPTIMIZE=0 bash "$SCRIPT_DIR/hyptest.sh" || status=1
OPTIMIZE=1 BOOST_TEST_ARGS='-t !smtCheckerTests' bash "$SCRIPT_DIR/hyptest.sh" || status=1
exit "$status"
