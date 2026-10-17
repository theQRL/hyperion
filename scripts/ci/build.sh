#!/usr/bin/env bash
set -euo pipefail

ROOTDIR="$(dirname "$0")/../.."
cd "${ROOTDIR}"
bash scripts/ci/prepare_metadata.sh "${1:-ci}"
IFS=' ' read -r -a cmake_options <<< "${CMAKE_OPTIONS:-}"
if [[ -n ${COVERAGE:-} && ${GITHUB_REF_TYPE:-} != tag ]]; then
    cmake_options+=(-DCOVERAGE=ON)
fi
cmake -S . -B "${HYPERION_BUILD_DIR:-build}" \
    -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE:-Release}" "${cmake_options[@]}"
cmake --build "${HYPERION_BUILD_DIR:-build}" --parallel "${BUILD_JOBS:-2}"
