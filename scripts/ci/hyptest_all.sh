#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)

# The first pass includes SMT and option-independent tests. Subsequent passes
# exercise the optimizer and ABI coder without repeating the SMT suite.
OPTIMIZE=0 ABI_ENCODER_V1=0 bash "$SCRIPT_DIR/hyptest.sh"
OPTIMIZE=1 ABI_ENCODER_V1=0 BOOST_TEST_ARGS='-t !smtCheckerTests' bash "$SCRIPT_DIR/hyptest.sh"
OPTIMIZE=1 ABI_ENCODER_V1=1 BOOST_TEST_ARGS='-t !smtCheckerTests' bash "$SCRIPT_DIR/hyptest.sh"
