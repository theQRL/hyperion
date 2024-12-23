#!/usr/bin/env bash
set -eo pipefail

# shellcheck source=scripts/common.sh
source "${REPO_ROOT}/scripts/common.sh"
# shellcheck source=scripts/common_cmdline.sh
source "${REPO_ROOT}/scripts/common_cmdline.sh"

function test_via_ir_equivalence()
{
    (( $# <= 2 )) || fail "This function accepts at most two arguments."
    local hyperion_file="$1"
    local optimize_flag="$2"
    [[ $optimize_flag == --optimize || $optimize_flag == "" ]] || assertFail "The second argument must be --optimize if present."

    local output_file_prefix
    output_file_prefix=$(basename "$hyperion_file" .hyp)

    HYPTMPDIR=$(mktemp -d -t "cmdline-test-via-ir-equivalence-${output_file_prefix}-XXXXXX")
    pushd "$HYPTMPDIR" > /dev/null

    local optimizer_flags=()
    [[ $optimize_flag == "" ]] || optimizer_flags+=("$optimize_flag")
    [[ $optimize_flag == "" ]] || output_file_prefix+="_optimize"

    msg_on_error --no-stderr \
        "$HYPC" --ir-optimized --debug-info location "${optimizer_flags[@]}" "$hyperion_file" |
            stripCLIDecorations |
            split_on_empty_lines_into_numbered_files "$output_file_prefix" ".yul"

    local asm_output_two_stage asm_output_via_ir

    for yul_file in $(find . -name "${output_file_prefix}*.yul" | sort -V); do
        asm_output_two_stage+=$(
            msg_on_error --no-stderr \
                "$HYPC" --strict-assembly --asm "${optimizer_flags[@]}" --no-optimize-yul "$yul_file" |
                    stripCLIDecorations
        )
    done

    asm_output_via_ir=$(
        msg_on_error --no-stderr \
            "$HYPC" --via-ir --asm --debug-info location "${optimizer_flags[@]}" "$hyperion_file" |
                stripCLIDecorations
    )

    diff_values "$asm_output_two_stage" "$asm_output_via_ir" --ignore-space-change --ignore-blank-lines

    local bin_output_two_stage bin_output_via_ir

    for yul_file in $(find . -name "${output_file_prefix}*.yul" | sort -V); do
        bin_output_two_stage+=$(
            msg_on_error --no-stderr \
                "$HYPC" --strict-assembly --bin "${optimizer_flags[@]}" "$yul_file" --no-optimize-yul |
                    stripCLIDecorations
        )
    done

    bin_output_via_ir=$(
        msg_on_error --no-stderr \
            "$HYPC" --via-ir --bin "${optimizer_flags[@]}" "$hyperion_file" | stripCLIDecorations
    )

    diff_values "$bin_output_two_stage" "$bin_output_via_ir" --ignore-space-change --ignore-blank-lines

    popd > /dev/null
    rm -r "$HYPTMPDIR"
}

externalContracts=(
    externalTests/hypc-js/DAO/TokenCreation.hyp
    libhyperion/semanticTests/externalContracts/_prbmath/PRBMathSD59x18.hyp
    libhyperion/semanticTests/externalContracts/_prbmath/PRBMathUD60x18.hyp
    libhyperion/semanticTests/externalContracts/_stringutils/stringutils.hyp
    libhyperion/semanticTests/externalContracts/deposit_contract.hyp
    libhyperion/semanticTests/externalContracts/FixedFeeRegistrar.hyp
    libhyperion/semanticTests/externalContracts/snark.hyp
)

requiresOptimizer=(
    externalTests/hypc-js/DAO/TokenCreation.hyp
    libhyperion/semanticTests/externalContracts/deposit_contract.hyp
    libhyperion/semanticTests/externalContracts/FixedFeeRegistrar.hyp
    libhyperion/semanticTests/externalContracts/snark.hyp
)

for contractFile in "${externalContracts[@]}"
do
    if ! [[ "${requiresOptimizer[*]}" =~ $contractFile ]]
    then
        printTask "    - ${contractFile}"
        test_via_ir_equivalence "${REPO_ROOT}/test/${contractFile}"
    fi

    printTask "    - ${contractFile} (optimized)"
    test_via_ir_equivalence "${REPO_ROOT}/test/${contractFile}" --optimize
done
