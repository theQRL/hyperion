#!/usr/bin/env bash
set -euo pipefail

# shellcheck source=scripts/common.sh
source "${REPO_ROOT}/scripts/common.sh"

HYPTMPDIR=$(mktemp -d -t "cmdline-test-hypjson-via-fuzzer-XXXXXX")
cd "$HYPTMPDIR"

"$REPO_ROOT"/scripts/isolate_tests.py "$REPO_ROOT"/test/
"$REPO_ROOT"/scripts/isolate_tests.py "$REPO_ROOT"/docs/

echo ./*.hyp | xargs -P 4 -n 50 "${HYPERION_BUILD_DIR}/test/tools/hypfuzzer" --quiet --input-files
echo ./*.hyp | xargs -P 4 -n 50 "${HYPERION_BUILD_DIR}/test/tools/hypfuzzer" --without-optimizer --quiet --input-files

rm -r "$HYPTMPDIR"
