#!/usr/bin/env bash
set -euo pipefail

# shellcheck source=scripts/common.sh
source "${REPO_ROOT}/scripts/common.sh"

HYPTMPDIR=$(mktemp -d -t "cmdline-test-ast-import-export-XXXXXX")
cd "$HYPTMPDIR"
if ! "$REPO_ROOT/scripts/ASTImportTest.sh" ast
then
    rm -r "$HYPTMPDIR"
    fail
fi
rm -r "$HYPTMPDIR"
