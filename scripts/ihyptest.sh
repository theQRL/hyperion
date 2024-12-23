#!/usr/bin/env bash

set -e

REPO_ROOT="$(dirname "$0")"/..
exec "${REPO_ROOT}/build/test/tools/ihyptest" --testpath "${REPO_ROOT}/test"
