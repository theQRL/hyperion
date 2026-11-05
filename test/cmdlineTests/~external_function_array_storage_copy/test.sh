#!/usr/bin/env bash
set -euo pipefail

# shellcheck source=scripts/common.sh
source "${REPO_ROOT}/scripts/common.sh"

tmpdir=$(mktemp -d -t "cmdline-test-extfn-array-copy-XXXXXX")
trap 'rm -rf "$tmpdir"' EXIT

source_file="${tmpdir}/external_function_array_storage_copy.hyp"
asm_file="${tmpdir}/external_function_array_storage_copy.asm"

cat > "$source_file" <<'EOF'
contract DynamicCopy {
    function() external[] a;
    function() external[] b;

    function f() public {
        a = b;
    }
}

contract FixedCopy {
    function() external[3] a;
    function() external[3] b;

    function f() public {
        a = b;
    }
}
EOF

msg_on_error --no-stderr "$HYPC" --asm "$source_file" > "$asm_file"

# Drop the source location comments that annotate the Yul utility functions and collapse whitespace.
normalized_asm=$(sed 's|/\*[^*]*\*/||g' "$asm_file" | tr '\n' ' ' | tr -s '[:space:]' ' ')

# Regression check for legacy storage-to-storage copies of function() external[].
# External function pointers occupy two storage slots on QRVM: address + selector.
# The legacy code generator copies storage arrays through the same Yul utility function
# as the IR pipeline. Its loop must load the selector from slot+1, store it to slot+1
# and advance both pointers by 2 slots.
expected_selector_load='sload 0xffffffff 0x01 dup6 add sload and'
expected_selector_store='sstore 0xffffffff dup2 and 0x01 dup6 add sstore 0x02 dup6 add swap5 pop 0x02 dup5 add swap4 pop'

function count_occurrences
{
    local remaining_asm="$normalized_asm"
    local count=0
    while [[ "$remaining_asm" == *"$1"* ]]
    do
        count=$((count + 1))
        remaining_asm="${remaining_asm#*"$1"}"
    done
    echo "$count"
}

for expected_selector_copy in "$expected_selector_load" "$expected_selector_store"
do
    copy_count=$(count_occurrences "$expected_selector_copy")
    if (( copy_count < 2 ))
    then
        printError "Legacy storage-to-storage copy of function() external[] did not copy the selector slot."
        printError "Expected selector-copy sequence at least twice, for dynamic and fixed-size arrays, but found ${copy_count}."
        printError "Expected assembly sequence:"
        >&2 echo "$expected_selector_copy"
        printError "Actual assembly:"
        >&2 cat "$asm_file"
        fail
    fi
done
