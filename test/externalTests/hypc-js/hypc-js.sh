#!/usr/bin/env bash

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
# (c) 2019 hyperion contributors.
#------------------------------------------------------------------------------

set -e

source scripts/common.sh
source scripts/externalTests/common.sh

HYPJSON="$1"
VERSION="$2"
HYPCJS_CHECKOUT="$3" # optional

[[ $HYPJSON != "" && -f "$HYPJSON" && $VERSION != "" ]] || fail "Usage: $0 <path to hypjson.js> <version> [<path to hypc-js>]"

# TODO(rgeraldes24)
function hypcjs_test
{
    TEST_DIR=$(pwd)
    HYPCJS_INPUT_DIR="$TEST_DIR"/test/externalTests/hypc-js

    # set up hypc-js on the branch specified
    setup_hypc "$DIR" hypcjs "$HYPJSON" master hypc/ "$HYPCJS_CHECKOUT"
    cd hypc/

    printLog "Updating index.js file..."
    echo "require('./determinism.js');" >> test/index.js

    printLog "Copying determinism.js..."
    cp -f "$HYPCJS_INPUT_DIR/determinism.js" test/

    printLog "Copying contracts..."
    cp -Rf "$HYPCJS_INPUT_DIR/DAO" test/

    printLog "Copying SMTChecker tests..."
    # We do not copy all tests because that takes too long.
    cp -Rf "$TEST_DIR"/test/libhyperion/smtCheckerTests/external_calls test/smtCheckerTests/
    cp -Rf "$TEST_DIR"/test/libhyperion/smtCheckerTests/loops test/smtCheckerTests/
    cp -Rf "$TEST_DIR"/test/libhyperion/smtCheckerTests/invariants test/smtCheckerTests/

    # Update version (needed for some tests)
    echo "Updating package.json to version $VERSION"
    npm version --allow-same-version --no-git-tag-version "$VERSION"

    replace_version_pragmas

    printLog "Running test function..."
    npm test
}

external_test hypc-js hypcjs_test
