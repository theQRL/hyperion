#!/usr/bin/env bash
# Use the checked-out commit, including GitHub's PR merge commit, on every platform.
set -euo pipefail

REPO_ROOT=$(cd "$(dirname "$0")/../.." && pwd)
cd "$REPO_ROOT"

version=$(bash scripts/get_version.sh)
prerelease_source=${1:-ci}
tag=''
if [[ ${GITHUB_REF_TYPE:-} == tag ]]; then
    tag=${GITHUB_REF_NAME:?Missing tag name}
elif [[ ${GITHUB_ACTIONS:-} != true && -z ${FORCE_RELEASE:-} ]]; then
    tag=$(git describe --tags --exact-match --match 'v[0-9]*' HEAD 2>/dev/null || true)
fi
if [[ -n $tag ]]; then
    [[ $tag =~ ^v[0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z]+([.-][0-9A-Za-z]+)*)?$ ]] || { echo "Invalid release tag: $tag" >&2; exit 1; }
    tag=${tag#v}
    if [[ $tag != "$version" && $tag != "$version"-* ]]; then
        echo "Tag v${tag} does not match compiler version ${version}." >&2
        exit 1
    fi
    if [[ $tag == "$version" ]]; then
        : > prerelease.txt
    else
        printf '%s' "${tag#"$version"-}" > prerelease.txt
    fi
elif [[ -n ${FORCE_RELEASE:-} ]]; then
    : > prerelease.txt
else
    # Windows strftime does not support %-m/%-d. Remove padding in Bash instead.
    commit_date=$(TZ=UTC git show --quiet --date='format-local:%Y.%m.%d' --format=%cd HEAD)
    IFS=. read -r year month day <<< "$commit_date"
    printf '%s.%s.%d.%d\n' "$prerelease_source" "$year" "$((10#$month))" "$((10#$day))" > prerelease.txt
fi
git rev-parse HEAD > commit_hash.txt
