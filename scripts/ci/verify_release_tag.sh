#!/usr/bin/env bash
# Refuse to release from anything but a signed, annotated tag on a commit of the default branch.
#
# Release tags are cut from the command line with `git tag -s` (ReleaseChecklist.md), never from the
# GitHub release page, which creates unsigned lightweight tags. Together with the `v*` tag rulesets
# (no updates or deletions, creation by admins only) this keeps one version bound to one commit.
# See CIPH-HYPERION-28.
set -euo pipefail

: "${GH_REPO:?Missing GitHub repository}"
: "${RELEASE_TAG:?Missing release tag}"
: "${GITHUB_SHA:?Missing the commit this workflow run built}"

fail() {
    echo "::error::$*" >&2
    exit 1
}

[[ $RELEASE_TAG =~ ^v[0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z]+([.-][0-9A-Za-z]+)*)?$ ]] ||
    fail "Invalid release tag: $RELEASE_TAG"

ref=$(gh api "repos/$GH_REPO/git/ref/tags/$RELEASE_TAG" --jq '.object.type + " " + .object.sha')
read -r ref_type tag_sha <<< "$ref"
[[ $ref_type == tag ]] ||
    fail "$RELEASE_TAG is a lightweight tag. Release tags must be signed and annotated (git tag -s)."

tag=$(gh api "repos/$GH_REPO/git/tags/$tag_sha" \
    --jq '.tag + " " + .object.type + " " + .object.sha + " " + (.verification.verified | tostring) + " " + .verification.reason')
read -r tag_name target_type commit_sha verified reason <<< "$tag"
# The signed name inside the tag object must be this release: otherwise a validly signed tag object
# made for another version could be pushed again under a new tag name.
[[ $tag_name == "$RELEASE_TAG" ]] ||
    fail "The tag object behind $RELEASE_TAG is signed as $tag_name. Create a new signed tag named $RELEASE_TAG."
[[ $target_type == commit ]] || fail "$RELEASE_TAG points at a $target_type, not a commit."
[[ $commit_sha == "$GITHUB_SHA" ]] ||
    fail "$RELEASE_TAG points at $commit_sha, but this run built $GITHUB_SHA."
[[ $verified == true ]] ||
    fail "GitHub does not verify the signature on $RELEASE_TAG (reason: $reason). Sign it with a key registered on your GitHub account."

# compare/BASE...HEAD reports "behind" when HEAD is an ancestor of BASE.
default_branch=$(gh api "repos/$GH_REPO" --jq .default_branch)
status=$(gh api "repos/$GH_REPO/compare/$default_branch...$commit_sha?per_page=1" --jq .status)
[[ $status == identical || $status == behind ]] ||
    fail "$RELEASE_TAG ($commit_sha) is not on $default_branch (compare status: $status). Release only reviewed, merged commits."

echo "Release tag $RELEASE_TAG is annotated, its signature is verified, and it points at $commit_sha on $default_branch."
