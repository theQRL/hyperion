#!/usr/bin/env bash
set -euo pipefail

# shellcheck source=scripts/common.sh
source "${REPO_ROOT}/scripts/common.sh"

function test_hypc_assembly_output
{
    local input="${1}"
    local expected="${2}"
    IFS=" " read -r -a hypc_args <<< "${3}"

    local expected_object="object \"object\" { code ${expected} }"

    output=$(echo "${input}" | msg_on_error --no-stderr "$HYPC" - "${hypc_args[@]}")
    empty=$(echo "$output" | tr '\n' ' ' | tr -s ' ' | sed -ne "/${expected_object}/p")
    if [ -z "$empty" ]
    then
        printError "Incorrect assembly output. Expected: "
        >&2 echo -e "${expected}"
        printError "with arguments ${hypc_args[*]}, but got:"
        >&2 echo "${output}"
        fail
    fi
}

echo '{}' | msg_on_error --silent "$HYPC" - --assemble
echo '{}' | msg_on_error --silent "$HYPC" - --yul
echo '{}' | msg_on_error --silent "$HYPC" - --strict-assembly

# Test options above in conjunction with --optimize.
# Using both, --assemble and --optimize should fail.
echo '{}' | "$HYPC" - --assemble --optimize &>/dev/null && fail "hypc --assemble --optimize did not fail as expected."
echo '{}' | "$HYPC" - --yul --optimize &>/dev/null && fail "hypc --yul --optimize did not fail as expected."

# Test yul and strict assembly output
# Non-empty code results in non-empty binary representation with optimizations turned off,
# while it results in empty binary representation with optimizations turned on.
test_hypc_assembly_output "{ let x:u256 := 0:u256 mstore(0, x) }" "{ { let x := 0 mstore(0, x) } }" "--yul"
test_hypc_assembly_output "{ let x:u256 := bitnot(7:u256) mstore(0, x) }" "{ { let x := bitnot(7) mstore(0, x) } }" "--yul"
test_hypc_assembly_output "{ let t:bool := not(true) if t { mstore(0, 1) } }" "{ { let t:bool := not(true) if t { mstore(0, 1) } } }" "--yul"
test_hypc_assembly_output "{ let x := 0 mstore(0, x) }" "{ { let x := 0 mstore(0, x) } }" "--strict-assembly"
test_hypc_assembly_output "{ let x := 0 mstore(0, x) }" "{ { } }" "--strict-assembly --optimize"
