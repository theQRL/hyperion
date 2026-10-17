#!/usr/bin/env bash

#------------------------------------------------------------------------------
# This script builds the hyperion binary using Emscripten.
# Emscripten is a way to compile C/C++ to JavaScript.
#
# http://kripken.github.io/emscripten-site/
#
# First run install_dep.sh OUTSIDE of docker and then
# run this script inside a docker image trzeci/emscripten
#
# The documentation for hyperion is hosted at:
#
# https://github.com/theQRL/hyperion/tree/develop/docs
#
# ------------------------------------------------------------------------------
# This file is part of hyperion.
#
# hyperion is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# hyperion is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with hyperion.  If not, see <http://www.gnu.org/licenses/>
#
# (c) 2016 hyperion contributors.
#------------------------------------------------------------------------------

set -ev

SCRIPT_DIR="$(realpath "$(dirname "$0")/..")"
# shellcheck source=scripts/common.sh
source "${SCRIPT_DIR}/common.sh"

function build() {
    local build_dir="$1"
    local prerelease_source="${2:-ci}"

    cd "${SCRIPT_DIR}/.."
    bash scripts/ci/prepare_metadata.sh "$prerelease_source"

    # Disable warnings for unqualified `move()` calls, introduced and enabled by
    # default in clang-16 which is what the emscripten docker image uses.
    # Additionally, disable the warning for unknown warnings here, as this script is
    # also used with earlier clang versions.
    # TODO: This can be removed if and when all usages of `move()` in our codebase use the `std::` qualifier.
    CMAKE_CXX_FLAGS="-Wno-unqualified-std-cast-call"

    mkdir -p "$build_dir"
    cd "$build_dir"
    emcmake cmake \
        -DCMAKE_BUILD_TYPE=Release \
        -DBoost_USE_STATIC_LIBS=1 \
        -DBoost_USE_STATIC_RUNTIME=1 \
        -DCMAKE_CXX_FLAGS="${CMAKE_CXX_FLAGS}" \
        -DTESTS=0 \
    ..
    cmake --build . --target hypjson --parallel "${BUILD_JOBS:-2}"

    cd ..
    mkdir -p upload
    scripts/ci/pack_hypjson.sh "$build_dir/libhypc/hypjson.js" "$build_dir/libhypc/hypjson.wasm" upload/hypjson.js
    cp upload/hypjson.js ./

    OUTPUT_SIZE=$(ls -la hypjson.js)

    echo "Emscripten output size: $OUTPUT_SIZE"
}

function show_help() {
cat << EOF
Usage: ${0##*/} [-h|--help] [--build-dir DIR] [--prerelease-source prerelease_source]
Build Hyperion emscripten binary
    -h | --help          Display this help message
    --build-dir          The emscripten build directory
    --prerelease-source  The prerelease source string. E.g. 'nightly' or 'ci'.
EOF
}

function main() {
    local build_dir="emscripten_build"
    local prerelease_source=""

    while (( $# > 0 )); do
        case "$1" in
            -h|--help)
                show_help
                exit 0
                ;;
            --build-dir)
                [[ -n "$2" ]] || fail "Option --build-dir cannot be empty"
                build_dir="$2"
                shift 2
                ;;
            --prerelease-source)
                [[ -n "$2" ]] || fail "Option --prerelease-source cannot be empty"
                prerelease_source="$2"
                shift 2
                ;;
            *) fail "Invalid option: $1" ;;
        esac
    done
    build "$build_dir" "$prerelease_source"
}

main "$@"
