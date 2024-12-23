#!/usr/bin/env bash

#------------------------------------------------------------------------------
# Bash script to execute the Hyperion tests.
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
# (c) 2016 hyperion contributors.
#------------------------------------------------------------------------------

set -e

REPO_ROOT="$(dirname "$0")/.."
HYPERION_BUILD_DIR="${HYPERION_BUILD_DIR:-${REPO_ROOT}/build}"
IFS=" " read -r -a SMT_FLAGS <<< "$SMT_FLAGS"

# shellcheck source=scripts/common.sh
source "${REPO_ROOT}/scripts/common.sh"

WORKDIR=$(mktemp -d)
CMDLINE_PID=

function cleanup
{
    # ensure failing commands don't cause termination during cleanup (especially within safe_kill)
    set +e

    if [[ -n "$CMDLINE_PID" ]]
    then
        safe_kill "$CMDLINE_PID" "Commandline tests"
    fi

    echo "Cleaning up working directory ${WORKDIR} ..."
    rm -rf "$WORKDIR" || true
}
trap cleanup INT TERM

log_directory=""
no_smt=""
while [[ $# -gt 0 ]]
do
    case "$1" in
        --junit_report)
            if [ -z "$2" ]
            then
                echo "Usage: $0 [--junit_report <report_directory>] [--no-smt]"
                exit 1
            else
                log_directory="$2"
            fi
            shift
            shift
            ;;
        --no-smt)
            no_smt="--no-smt"
            SMT_FLAGS+=(--no-smt)
            shift
            ;;
        *)
            echo "Usage: $0 [--junit_report <report_directory>] [--no-smt]"
            exit 1
    esac
done

printTask "Testing Python scripts..."
"$REPO_ROOT/test/pyscriptTests.py"

printTask "Testing LSP..."
"$REPO_ROOT/test/lsp.py" "${HYPERION_BUILD_DIR}/hypc/hypc"

printTask "Running commandline tests..."
# Only run in parallel if this is run on CI infrastructure
if [[ -n "$CI" ]]
then
    "$REPO_ROOT/test/cmdlineTests.sh" &
    CMDLINE_PID=$!
else
    if ! "$REPO_ROOT/test/cmdlineTests.sh" "$no_smt"
    then
        printError "Commandline tests FAILED"
        exit 1
    fi
fi


ZVM_VERSIONS="shanghai"

# And then run the Hyperion unit-tests in the matrix combination of optimizer / no optimizer
# and shanghai VM
for optimize in "" "--optimize"
do
    for vm in $ZVM_VERSIONS
    do
        FORCE_ABIV1_RUNS="no yes"
        for abiv1 in $FORCE_ABIV1_RUNS
        do
            force_abiv1_flag=()
            if [[ "$abiv1" == "yes" ]]
            then
                force_abiv1_flag=(--abiencoderv1)
            fi
            printTask "--> Running tests using $optimize --zvm-version $vm ${force_abiv1_flag[*]}..."

            log=()
            if [ -n "$log_directory" ]
            then
                if [ -n "$optimize" ]
                then
                    log+=("--logger=JUNIT,error,$log_directory/opt_$vm.xml")
                else
                    log+=("--logger=JUNIT,error,$log_directory/noopt_$vm.xml")
                fi
            fi

            set +e
            "${HYPERION_BUILD_DIR}"/test/hyptest --show-progress "${log[@]}" -- --testpath "$REPO_ROOT"/test "$optimize" --zvm-version "$vm" "${SMT_FLAGS[@]}" "${force_abiv1_flag[@]}"

            if test "0" -ne "$?"; then
                exit 1
            fi
            set -e

        done
    done
done

if [[ -n $CMDLINE_PID ]] && ! wait $CMDLINE_PID
then
    printError "Commandline tests FAILED"
    CMDLINE_PID=
    exit 1
fi

cleanup
