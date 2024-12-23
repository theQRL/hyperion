#!/usr/bin/env bash

#------------------------------------------------------------------------------
# Bash script to run commandline Hyperion tests.
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

set -eo pipefail

## GLOBAL VARIABLES

REPO_ROOT=$(cd "$(dirname "$0")/.." && pwd)
HYPERION_BUILD_DIR=${HYPERION_BUILD_DIR:-${REPO_ROOT}/build}
export REPO_ROOT HYPERION_BUILD_DIR

# shellcheck source=scripts/common.sh
source "${REPO_ROOT}/scripts/common.sh"
# shellcheck source=scripts/common_cmdline.sh
source "${REPO_ROOT}/scripts/common_cmdline.sh"

pushd "${REPO_ROOT}/test/cmdlineTests" > /dev/null
autoupdate=false
no_smt=false
declare -a included_test_patterns
declare -a excluded_test_patterns
while [[ $# -gt 0 ]]
do
    case "$1" in
        --update)
            autoupdate=true
            shift
            ;;
        --no-smt)
            no_smt=true
            shift
            ;;
        --exclude)
            [[ $2 != '' ]] || fail "No pattern given to --exclude option or the pattern is empty."
            excluded_test_patterns+=("$2")
            shift
            shift
            ;;
        *)
            included_test_patterns+=("$1")
            shift
            ;;
    esac
done

(( ${#included_test_patterns[@]} > 0 )) || included_test_patterns+=('*')

test_name_filter=('(' -name "${included_test_patterns[0]}")
for pattern in "${included_test_patterns[@]:1}"
do
    test_name_filter+=(-or -name "$pattern")
done
test_name_filter+=(')')

for pattern in "${excluded_test_patterns[@]}"
do
    test_name_filter+=(-and -not -name "$pattern")
done

# NOTE: We want leading symbols in names to affect the sort order but without
# LC_COLLATE=C sort seems to ignore them.
# shellcheck disable=SC2207 # We do not support test names containing spaces.
selected_tests=($(find . -mindepth 1 -maxdepth 1 -type d "${test_name_filter[@]}" | cut -c 3- | LC_COLLATE=C sort))

if (( ${#selected_tests[@]} == 0 ))
then
    printWarning "The pattern '${test_name_filter[*]}' did not match any tests."
    exit 0;
else
    test_count=$(find . -mindepth 1 -maxdepth 1 -type d | wc -l)
    printLog "Selected ${#selected_tests[@]} out of ${test_count} tests."
fi

popd > /dev/null

case "$OSTYPE" in
    msys)
        HYPC="${HYPERION_BUILD_DIR}/hypc/Release/hypc.exe"

        # prevents msys2 path translation for a remapping test
        export MSYS2_ARG_CONV_EXCL="="
        ;;
    *)
        HYPC="${HYPERION_BUILD_DIR}/hypc/hypc"
        ;;
esac
echo "Using hypc binary at ${HYPC}"
export HYPC

INTERACTIVE=true
if ! tty -s || [ "$CI" ]
then
    INTERACTIVE=false
fi

# extend stack size in case we run via ASAN
if [[ -n "${CIRCLECI}" ]] || [[ -n "$CI" ]]
then
    ulimit -s 16384
    ulimit -a
fi

## FUNCTIONS

function update_expectation {
    local newExpectation="${1}"
    local expectationFile="${2}"

    if [[ $newExpectation == '' || $newExpectation == '0' && $expectationFile == */exit ]]
    then
        if [[ -f $expectationFile ]]
        then
            rm "$expectationFile"
        fi
        return
    fi

    echo "$newExpectation" > "$expectationFile"
    printLog "File $expectationFile updated to match the expectation."
}

function ask_expectation_update
{
    if [[ $INTERACTIVE == true ]]
    then
        local newExpectation="${1}"
        local expectationFile="${2}"

        if [[ $autoupdate == true ]]
        then
            update_expectation "$newExpectation" "$expectationFile"
        else
            local editor="${FCEDIT:-${VISUAL:-${EDITOR:-vi}}}"

            while true
            do
                read -r -n 1 -p "(e)dit/(u)pdate expectations/(s)kip/(q)uit? "
                echo
                case $REPLY in
                    e*) "$editor" "$expectationFile"; break;;
                    u*) update_expectation "$newExpectation" "$expectationFile"; break;;
                    s*) return;;
                    q*) fail;;
                esac
            done
        fi
    else
        [[ $INTERACTIVE == false ]] || assertFail
        fail
    fi
}

# General helper function for testing HYPC behaviour, based on file name, compile opts, exit code, stdout and stderr.
# An failure is expected.
function test_hypc_behaviour
{
    local filename="${1}"
    local hypc_args
    IFS=" " read -r -a hypc_args <<< "${2}"
    local hypc_stdin="${3}"
    [ -z "$hypc_stdin"  ] && hypc_stdin="/dev/stdin"
    local stdout_expected="${4}"
    local exit_code_expected="${5}"
    local exit_code_expectation_file="${6}"
    local stderr_expected="${7}"
    local stdout_expectation_file="${8}" # the file to write to when user chooses to update stdout expectation
    local stderr_expectation_file="${9}" # the file to write to when user chooses to update stderr expectation
    local stdout_path; stdout_path=$(mktemp -t "cmdline-test-stdout-XXXXXX")
    local stderr_path; stderr_path=$(mktemp -t "cmdline-test-stderr-XXXXXX")

    # shellcheck disable=SC2064
    trap "rm -f $stdout_path $stderr_path" EXIT

    if [[ "$exit_code_expected" = "" ]]
    then
        exit_code_expected="0"
    fi

    [[ $filename == "" ]] || hypc_args+=("$filename")

    local hypc_command="$HYPC ${hypc_args[*]} <$hypc_stdin"
    set +e
    "$HYPC" "${hypc_args[@]}" <"$hypc_stdin" >"$stdout_path" 2>"$stderr_path"
    exitCode=$?
    set -e

    if [[ " ${hypc_args[*]} " == *" --standard-json "* ]] && [[ -s $stdout_path ]]
    then
        python3 - <<EOF
import re, sys
json = open("$stdout_path", "r").read()
json = re.sub(r"{[^{}]*Warning: This is a pre-release compiler version[^{}]*},?", "", json)
json = re.sub(r"\"errors\":\s*\[\s*\],?","\n" if json[1] == " " else "",json)       # Remove "errors" array if it's not empty
json = re.sub("\n\\s*\n", "\n", json)                                               # Remove trailing whitespace
json = re.sub(r"},(\n{0,1})\n*(\s*(]|}))", r"}\1\2", json)                          # Remove trailing comma
open("$stdout_path", "w").write(json)
EOF
        sed -i.bak -E -e 's/ Consider adding \\"pragma hyperion \^[0-9.]*;\\"//g' "$stdout_path"
        sed -i.bak -E -e 's/\"opcodes\":[[:space:]]*\"[^"]+\"/\"opcodes\":\"<OPCODES REMOVED>\"/g' "$stdout_path"
        sed -i.bak -E -e 's/\"sourceMap\":[[:space:]]*\"[0-9:;-]+\"/\"sourceMap\":\"<SOURCEMAP REMOVED>\"/g' "$stdout_path"

        # Remove bytecode (but not linker references).
        sed -i.bak -E -e 's/(\"object\":[[:space:]]*\")[0-9a-f]+([^"]*\")/\1<BYTECODE REMOVED>\2/g' "$stdout_path"
        # shellcheck disable=SC2016
        sed -i.bak -E -e 's/(\"object\":[[:space:]]*\"[^"]+\$__)[0-9a-f]+(\")/\1<BYTECODE REMOVED>\2/g' "$stdout_path"
        # shellcheck disable=SC2016
        sed -i.bak -E -e 's/([0-9a-f]{34}\$__)[0-9a-f]+(__\$[0-9a-f]{17})/\1<BYTECODE REMOVED>\2/g' "$stdout_path"
        # Remove metadata in assembly output (see below about the magic numbers)
        sed -i.bak -E -e 's/"[0-9a-f]+64697066735822[0-9a-f]+6468797063[0-9a-f]+/"<BYTECODE REMOVED>/g' "$stdout_path"

        # Replace escaped newlines by actual newlines for readability
        # shellcheck disable=SC1003
        sed -i.bak -E -e 's/\\n/\'$'\n/g' "$stdout_path"
        sed -i.bak -e 's/\(^[ ]*auxdata:[[:space:]]\)0x[0-9a-f]*$/\1<AUXDATA REMOVED>/' "$stdout_path"
        sed -i.bak -e 's/\(\\"version\\":[ ]*\\"\)[^"\\]*\(\\"\)/\1<VERSION REMOVED>\2/' "$stdout_path"
        rm "$stdout_path.bak"
    else
        sed -i.bak -e '/^Warning: This is a pre-release compiler version, please do not use it in production./,+1d' "$stderr_path"
        sed -i.bak -e '/^Compiler run successful, no output requested\.$/d' "$stderr_path"
        sed -i.bak -e '/^Warning (3805): This is a pre-release compiler version, please do not use it in production./,+1d' "$stderr_path"
        sed -i.bak -e 's/\(^[ ]*auxdata: \)0x[0-9a-f]*$/\1<AUXDATA REMOVED>/' "$stdout_path"
        sed -i.bak -e 's/ Consider adding "pragma .*$//' "$stderr_path"
        sed -i.bak -e 's/\(Unimplemented feature error.* in \).*$/\1<FILENAME REMOVED>/' "$stderr_path"
        sed -i.bak -e 's/"version":[ ]*"[^"]*"/"version": "<VERSION REMOVED>"/' "$stdout_path"
    if [[ $stdout_expectation_file != "" &&  $stderr_expectation_file != "" ]]
    then
        sed -i.bak -e '/^Compiler run successful\. No contracts to compile\.$/d' "$stdout_path"
        sed -i.bak -e '/^Compiler run successful\. No output generated\.$/d' "$stdout_path"
    fi

        # Remove bytecode (but not linker references). Since non-JSON output is unstructured,
        # use metadata markers for detection to have some confidence that it's actually bytecode
        # and not some random word.
        # 64697066735822 = hex encoding of 0x64 'i' 'p' 'f' 's' 0x58 0x22
        # 6468797063     = hex encoding of 0x64 'h' 'y' 'p' 'c'
        sed -i.bak -E -e 's/[0-9a-f]*64697066735822[0-9a-f]+6468797063[0-9a-f]+/<BYTECODE REMOVED>/g' "$stdout_path"
        # shellcheck disable=SC2016
        sed -i.bak -E -e 's/([0-9a-f]{17}\$__)[0-9a-f]+(__\$[0-9a-f]{17})/\1<BYTECODE REMOVED>\2/g' "$stdout_path"
        # shellcheck disable=SC2016
        sed -i.bak -E -e 's/[0-9a-f]+((__\$[0-9a-f]{34}\$__)*<BYTECODE REMOVED>)/<BYTECODE REMOVED>\1/g' "$stdout_path"

        # Remove trailing empty lines. Needs a line break to make OSX sed happy.
        sed -i.bak -e '1{/^$/d
}' "$stderr_path"
        rm "$stderr_path.bak" "$stdout_path.bak"
    fi
    # Remove path to cpp file
    sed -i.bak -e 's/^\(Exception while assembling:\).*/\1/' "$stderr_path"
    # Remove exception class name.
    sed -i.bak -e 's/^\(Dynamic exception type:\).*/\1/' "$stderr_path"
    rm "$stderr_path.bak"

    if [[ $exitCode -ne "$exit_code_expected" ]]
    then
        printError "Incorrect exit code. Expected $exit_code_expected but got $exitCode."

        [[ $exit_code_expectation_file != "" ]] && ask_expectation_update "$exitCode" "$exit_code_expectation_file"
        [[ $exit_code_expectation_file == "" ]] && fail
    fi

    if [[ "$(cat "$stdout_path")" != "${stdout_expected}" ]]
    then
        printError "Incorrect output on stdout received. Expected:"
        echo -e "${stdout_expected}"

        printError "But got:"
        echo -e "$(cat "$stdout_path")"

        printError "When running $hypc_command"

        [[ $stdout_expectation_file != "" ]] && ask_expectation_update "$(cat "$stdout_path")" "$stdout_expectation_file"
        [[ $stdout_expectation_file == "" ]] && fail
    fi

    if [[ "$(cat "$stderr_path")" != "${stderr_expected}" ]]
    then
        printError "Incorrect output on stderr received. Expected:"
        echo -e "${stderr_expected}"

        printError "But got:"
        echo -e "$(cat "$stderr_path")"

        printError "When running $hypc_command"

        [[ $stderr_expectation_file != "" ]] && ask_expectation_update "$(cat "$stderr_path")" "$stderr_expectation_file"
        [[ $stderr_expectation_file == "" ]] && fail
    fi

    rm "$stdout_path" "$stderr_path"
}


## RUN

printTask "Testing passing files that are not found..."
test_hypc_behaviour "file_not_found.hyp" "" "" "" 1 "" "Error: \"file_not_found.hyp\" is not found." "" ""

printTask "Testing passing files that are not files..."
test_hypc_behaviour "." "" "" "" 1 "" "Error: \".\" is not a valid file." "" ""

printTask "Testing passing empty remappings..."
test_hypc_behaviour "${0}" "=/some/remapping/target" "" "" 1 "" "Error: Invalid remapping: \"=/some/remapping/target\"." "" ""
test_hypc_behaviour "${0}" "ctx:=/some/remapping/target" "" "" 1 "" "Error: Invalid remapping: \"ctx:=/some/remapping/target\"." "" ""

printTask "Running general commandline tests..."
(
    cd "$REPO_ROOT"/test/cmdlineTests/
    for tdir in "${selected_tests[@]}"
    do
        if ! [[ -d $tdir ]]
        then
            fail "Test directory not found: $tdir"
        fi

        printTask " - ${tdir}"

        if [[ $(ls -A "$tdir") == "" ]]
        then
            printWarning "   ---> skipped (test dir empty)"
            continue
        fi

        # Strip trailing slash from $tdir.
        tdir=$(basename "${tdir}")
        if [[ $no_smt == true ]]
        then
            if [[ $tdir =~ .*model_checker_.* ]]
            then
                printWarning "   ---> skipped (SMT test)"
                continue
            fi
        fi

        scriptFiles="$(ls -1 "${tdir}/test."* 2> /dev/null || true)"
        scriptCount="$(echo "${scriptFiles}" | wc -w)"

        inputFiles="$(ls -1 "${tdir}/input."* 2> /dev/null || true)"
        inputCount="$(echo "${inputFiles}" | wc -w)"
        (( inputCount <= 1 )) || fail "Ambiguous input. Found input files in multiple formats:"$'\n'"${inputFiles}"
        (( scriptCount <= 1 )) || fail "Ambiguous input. Found script files in multiple formats:"$'\n'"${scriptFiles}"
        (( inputCount == 0 || scriptCount == 0 )) || fail "Ambiguous input. Found both input and script files:"$'\n'"${inputFiles}"$'\n'"${scriptFiles}"

        if (( scriptCount == 1 ))
        then
            if ! "$scriptFiles"
            then
                fail "Test script ${scriptFiles} failed."
            fi

            continue
        fi

        # Use printf to get rid of the trailing newline
        inputFile=$(printf "%s" "${inputFiles}")

        if [ "${inputFile}" = "${tdir}/input.json" ]
        then
            ! [ -e "${tdir}/stdin" ] || fail "Found a file called 'stdin' but redirecting standard input in JSON mode is not allowed."

            stdin="${inputFile}"
            inputFile=""
            stdout="$(cat "${tdir}/output.json" 2>/dev/null || true)"
            stdoutExpectationFile="${tdir}/output.json"
            prettyPrintFlags=""
            if [[ ! -f "${tdir}/no-pretty-print" ]]
            then
                prettyPrintFlags="--pretty-json --json-indent 4"
            fi

            command_args="--standard-json ${prettyPrintFlags} "$(cat "${tdir}/args" 2>/dev/null || true)
        else
            if [ -e "${tdir}/stdin" ]
            then
                stdin="${tdir}/stdin"
                [ -f "${tdir}/stdin" ] || fail "'stdin' is not a regular file."
            else
                stdin=""
            fi

            stdout="$(cat "${tdir}/output" 2>/dev/null || true)"
            stdoutExpectationFile="${tdir}/output"
            command_args=$(cat "${tdir}/args" 2>/dev/null || true)
        fi
        exitCodeExpectationFile="${tdir}/exit"
        exitCode=$(cat "$exitCodeExpectationFile" 2>/dev/null || true)
        err="$(cat "${tdir}/err" 2>/dev/null || true)"
        stderrExpectationFile="${tdir}/err"
        test_hypc_behaviour "$inputFile" \
                            "$command_args" \
                            "$stdin" \
                            "$stdout" \
                            "$exitCode" \
                            "$exitCodeExpectationFile" \
                            "$err" \
                            "$stdoutExpectationFile" \
                            "$stderrExpectationFile"
    done
)

echo "Commandline tests successful."
