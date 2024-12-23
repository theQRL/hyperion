#!/usr/bin/env bash
set -euo pipefail

# shellcheck source=scripts/common.sh
source "${REPO_ROOT}/scripts/common.sh"

HYPTMPDIR=$(mktemp -d -t "cmdline-test-linking-XXXXXX")
cd "$HYPTMPDIR"

echo 'library L { function f() public pure {} } contract C { function f() public pure { L.f(); } }' > x.hyp
msg_on_error --no-stderr "$HYPC" --bin -o . x.hyp

# Explanation and placeholder should be there
grep -q '//' C.bin && grep -q '__' C.bin

# But not in library file.
grep -q -v '[/_]' L.bin

# Now link
printf "    "
msg_on_error "$HYPC" --link --libraries x.hyp:L=0x90f20564390eAe531E810af625A22f51385Cd222 C.bin

# Now the placeholder and explanation should be gone.
grep -q -v '[/_]' C.bin

rm -r "$HYPTMPDIR"
