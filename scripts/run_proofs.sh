#!/usr/bin/env bash
# Prove changed rules without fetching or assuming a particular branch name.
set -euo pipefail
REPO_ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$REPO_ROOT"
base_ref=${1:-${PROOF_BASE_REF:-HEAD^}}
if git rev-parse --verify "$base_ref^{commit}" >/dev/null 2>&1; then
    mapfile -d '' -t proofs < <(git diff --name-only --diff-filter=AM -z "$base_ref" HEAD -- 'test/formal/*.py')
else
    mapfile -d '' -t proofs < <(git ls-files -z -- 'test/formal/*.py')
fi
status=0
for proof in "${proofs[@]}"; do
    echo "Proving $proof..."
    python3 "$proof" || status=1
done
exit "$status"
