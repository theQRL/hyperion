#!/usr/bin/env bash
set -euo pipefail

# shellcheck source=scripts/common.sh
source "${REPO_ROOT}/scripts/common.sh"

echo '' | msg_on_error --no-stdout "$HYPC" - --link --libraries a=Q90f20564390eae531e810af625a22f51385cd22200000000000000000000000000000000000000000000000000000000
echo '' | "$HYPC" - --link --libraries a=Q80f20564390eae531e810af625a22f51385cd2220000000000000000000000000000000000000000000000000000000z &>/dev/null && \
    fail "hypc --link did not reject an invalid library address."

exit 0
