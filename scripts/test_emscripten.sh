#!/usr/bin/env bash

#------------------------------------------------------------------------------
# Bash script to execute the Hyperion tests using the emscripten binary.
#
# The documentation for hyperion is hosted at:
#
#     https://docs.soliditylang.org
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
# (c) 2017 hyperion contributors.
#------------------------------------------------------------------------------

set -e

if test -z "$1"; then
	BUILD_DIR="emscripten_build"
else
	BUILD_DIR="$1"
fi

REPO_ROOT=$(cd "$(dirname "$0")/.." && pwd)
HYPJSON="$REPO_ROOT/$BUILD_DIR/libhypc/hypjson.js"
VERSION=$("$REPO_ROOT"/scripts/get_version.sh)

echo "Running hypcjs tests...."
"$REPO_ROOT/test/externalTests/hypc-js/hypc-js.sh" "$HYPJSON" "$VERSION"
