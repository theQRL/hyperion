#! /bin/bash
#------------------------------------------------------------------------------
# Bash script to execute the Solidity tests by CircleCI.
#
# The documentation for solidity is hosted at:
#
#     https://docs.soliditylang.org
#
# ------------------------------------------------------------------------------
# This file is part of solidity.
#
# solidity is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# solidity is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with solidity.  If not, see <http://www.gnu.org/licenses/>
#
# (c) 2016-2019 solidity contributors.
# ------------------------------------------------------------------------------
set -e

REPODIR="$(realpath "$(dirname "$0")"/..)"

# shellcheck source=scripts/common.sh
source "${REPODIR}/scripts/common.sh"

QRVM_VALUES=(zond)
DEFAULT_QRVM=zond
[[ " ${QRVM_VALUES[*]} " =~ $DEFAULT_QRVM ]]
OPTIMIZE_VALUES=(0 1)

# Run for ABI encoder v1, without SMTChecker tests.
QRVM="${DEFAULT_QRVM}" \
OPTIMIZE=1 \
ABI_ENCODER_V1=1 \
BOOST_TEST_ARGS="-t !smtCheckerTests" \
"${REPODIR}/.circleci/hyptest.sh"

# We shift the batch index so that long-running tests
# do not always run in the same executor for all QRVM versions
INDEX_SHIFT=0
for OPTIMIZE in "${OPTIMIZE_VALUES[@]}"
do
    for QRVM in "${QRVM_VALUES[@]}"
    do
        ENFORCE_GAS_ARGS=""
        [ "${QRVM}" = "${DEFAULT_QRVM}" ] && ENFORCE_GAS_ARGS="--enforce-gas-cost"
        # Run SMTChecker tests only when OPTIMIZE == 0
        DISABLE_SMTCHECKER=""
        [ "${OPTIMIZE}" != "0" ] && DISABLE_SMTCHECKER="-t !smtCheckerTests"

        QRVM="$QRVM" \
        OPTIMIZE="$OPTIMIZE" \
        HYPTEST_FLAGS="$HYPTEST_FLAGS $ENFORCE_GAS_ARGS" \
        BOOST_TEST_ARGS="-t !@nooptions $DISABLE_SMTCHECKER" \
        INDEX_SHIFT="$INDEX_SHIFT" \
        "${REPODIR}/.circleci/hyptest.sh"

        INDEX_SHIFT=$((INDEX_SHIFT + 1))
    done
done
