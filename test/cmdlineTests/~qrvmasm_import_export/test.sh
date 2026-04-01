#!/usr/bin/env bash
set -euo pipefail

# shellcheck source=scripts/common.sh
source "${REPO_ROOT}/scripts/common.sh"

HYPTMPDIR=$(mktemp -d -t "cmdline-test-qrvmasm-import-export-XXXXXX")
cd "$HYPTMPDIR"
if ! "$REPO_ROOT/scripts/ASTImportTest.sh" qrvm-assembly
then
    rm -r "$HYPTMPDIR"
    fail
fi
rm -r "$HYPTMPDIR"
