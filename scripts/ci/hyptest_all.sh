#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)

# The first pass includes SMT and option-independent tests. The second pass
# exercises the optimizer without repeating the SMT suite.
OPTIMIZE=0 ABI_ENCODER_V1=0 bash "$SCRIPT_DIR/hyptest.sh"
OPTIMIZE=1 ABI_ENCODER_V1=0 BOOST_TEST_ARGS='-t !smtCheckerTests' bash "$SCRIPT_DIR/hyptest.sh"

# Shared fixtures use ABI v2 expectations, including its stricter validation of
# malformed calldata. Run the explicit ABI v1 fixtures with their own settings.
abi_v1_tests=$(python3 - "$SCRIPT_DIR/../../test/libhyperion" <<'PY'
from pathlib import Path
import re
import sys

root = Path(sys.argv[1])
tests = []
for path in sorted((root / 'semanticTests').rglob('*.hyp')):
    source = path.read_text(encoding='utf8')
    if re.search(r'^\s*pragma\s+abicoder\s+v1\s*;', source, re.MULTILINE):
        tests.append(path.relative_to(root).with_suffix('').as_posix())
if not tests:
    raise SystemExit('No ABI v1 regression tests found.')
print(':'.join(tests))
PY
)
OPTIMIZE=1 ABI_ENCODER_V1=1 BOOST_TEST_ARGS="-t $abi_v1_tests" bash "$SCRIPT_DIR/hyptest.sh"
