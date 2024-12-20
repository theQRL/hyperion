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

# Requires $REPO_ROOT to be defined and "${REPO_ROOT}/scripts/common.sh" to be included before.

CURRENT_ZVM_VERSION=shanghai

AVAILABLE_PRESETS=(
    legacy-no-optimize
    ir-no-optimize
    legacy-optimize-zvm-only
    ir-optimize-zvm-only
    legacy-optimize-zvm+yul
    ir-optimize-zvm+yul
)

function print_presets_or_exit
{
    local selected_presets="$1"

    [[ $selected_presets != "" ]] || { printWarning "No presets to run. Exiting."; exit 0; }

    printLog "Selected settings presets: ${selected_presets}"
}

function verify_input
{
    local binary_type="$1"
    local binary_path="$2"
    local selected_presets="$3"

    (( $# >= 2 && $# <= 3 )) || fail "Usage: $0 native|hypcjs <path to hypc or hypjson.js> [preset]"
    [[ $binary_type == native || $binary_type == hypcjs ]] || fail "Invalid binary type: '${binary_type}'. Must be either 'native' or 'hypcjs'."
    [[ -f "$binary_path" ]] || fail "The compiler binary does not exist at '${binary_path}'"

    if [[ $selected_presets != "" ]]
    then
        for preset in $selected_presets
        do
            if [[ " ${AVAILABLE_PRESETS[*]} " != *" $preset "* ]]
            then
                fail "Preset '${preset}' does not exist. Available presets: ${AVAILABLE_PRESETS[*]}."
            fi
        done
    fi
}

function setup_hypc
{
    local test_dir="$1"
    local binary_type="$2"
    local binary_path="$3"
    local hypcjs_branch="${4:-master}"
    local install_dir="${5:-hypc/}"
    local hypcjs_dir="$6"

    [[ $binary_type == native || $binary_type == hypcjs ]] || assertFail
    [[ $binary_type == hypcjs || $hypcjs_dir == "" ]] || assertFail

    cd "$test_dir"

    if [[ $binary_type == hypcjs ]]
    then
        printLog "Setting up hypc-js..."
        if [[ $hypcjs_dir == "" ]]; then
            printLog "Cloning branch ${hypcjs_branch}..."
            # TODO(rgeraldes24)
            git clone --depth 1 -b "$hypcjs_branch" https://github.com/ethereum/hypc-js.git "$install_dir"
        else
            printLog "Using local hypc-js from ${hypcjs_dir}..."
            cp -ra "$hypcjs_dir" hypc
        fi

        pushd "$install_dir"
        npm install
        cp "$binary_path" hypjson.js
        npm run build
        HYPCVERSION=$(dist/hypc.js --version)
        popd
    else
        printLog "Setting up hypc..."
        HYPCVERSION=$("$binary_path" --version | tail -n 1 | sed -n -E 's/^Version: (.*)$/\1/p')
    fi

    HYPCVERSION=$(echo "$HYPCVERSION" | sed -En 's/^([0-9.]+).*\+commit\.[0-9a-f]+.*$/\1/p')
    printLog "Using compiler version $HYPCVERSION"
}

function download_project
{
    local repo="$1"
    local ref_type="$2"
    local hypcjs_ref="$3"
    local test_dir="$4"

    [[ $ref_type == commit || $ref_type == branch || $ref_type == tag ]] || assertFail

    printLog "Cloning ${ref_type} ${hypcjs_ref} of ${repo}..."
    if [[ $ref_type == commit ]]; then
        mkdir ext
        cd ext
        git init
        git remote add origin "$repo"
        git fetch --depth 1 origin "$hypcjs_ref"
        git reset --hard FETCH_HEAD
    else
        git clone --depth 1 "$repo" -b "$hypcjs_ref" "$test_dir/ext"
        cd ext
    fi
    echo "Current commit hash: $(git rev-parse HEAD)"
}

function force_truffle_version
{
    local version="$1"

    sed -i 's/"truffle":\s*".*"/"truffle": "'"$version"'"/g' package.json
}

function replace_version_pragmas
{
    # Replace fixed-version pragmas (part of Consensys best practice).
    # Include all directories to also cover node dependencies.
    printLog "Replacing fixed-version pragmas..."
    find . test -name '*.hyp' -type f -print0 | xargs -0 sed -i -E -e 's/pragma hyperion [^;]+;/pragma hyperion >=0.0;/'
}

function neutralize_package_lock
{
    # Remove lock files (if they exist) to prevent them from overriding our changes in package.json
    printLog "Removing package lock files..."
    rm --force --verbose yarn.lock
    rm --force --verbose package-lock.json
}

function neutralize_package_json_hooks
{
    printLog "Disabling package.json hooks..."
    [[ -f package.json ]] || fail "package.json not found"
    sed -i 's|"prepublish": *".*"|"prepublish": ""|g' package.json
    sed -i 's|"prepare": *".*"|"prepare": ""|g' package.json
}

function neutralize_packaged_contracts
{
    # Frameworks will build contracts from any package that contains a configuration file.
    # This is both unnecessary (any files imported from these packages will get compiled again as a
    # part of the main project anyway) and trips up our version check because it won't use our
    # custom compiler binary.
    printLog "Removing framework config and artifacts from npm packages..."
    find node_modules/ -type f '(' -name 'hardhat.config.*' -o -name 'truffle-config.*' ')' -delete

    # Some npm packages also come packaged with pre-built artifacts.
    find node_modules/ -path '*artifacts/build-info/*.json' -delete
}

function force_hypc_modules
{
    local custom_hypcjs_path="${1:-hypc/}"

    [[ -d node_modules/ ]] || assertFail

    printLog "Replacing all installed hypc-js with a link to the latest version..."
    hypjson_binaries=$(find node_modules -type f -path "*/hypc/hypjson.js")
    for hypjson_binary in $hypjson_binaries
    do
        local hypc_module_path
        hypc_module_path=$(dirname "$hypjson_binary")

        printLog "Found and replaced hypc-js in $hypc_module_path"
        rm -r "$hypc_module_path"
        ln -s "$custom_hypcjs_path" "$hypc_module_path"
    done
}

function force_truffle_compiler_settings
{
    local config_file="$1"
    local binary_type="$2"
    local hypc_path="$3"
    local preset="$4"
    local zvm_version="${5:-"$CURRENT_ZVM_VERSION"}"
    local extra_settings="$6"
    local extra_optimizer_settings="$7"

    [[ $binary_type == native || $binary_type == hypcjs ]] || assertFail

    [[ $binary_type == native ]] && local hypc_path="native"

    printLog "Forcing Truffle compiler settings..."
    echo "-------------------------------------"
    echo "Config file: $config_file"
    echo "Binary type: $binary_type"
    echo "Compiler path: $hypc_path"
    echo "Settings preset: ${preset}"
    echo "Settings: $(settings_from_preset "$preset" "$zvm_version" "$extra_settings" "$extra_optimizer_settings")"
    echo "ZVM version: $zvm_version"
    echo "Compiler version: ${HYPCVERSION_SHORT}"
    echo "Compiler version (full): ${HYPCVERSION}"
    echo "-------------------------------------"

    local compiler_settings gas_reporter_settings
    compiler_settings=$(truffle_compiler_settings "$hypc_path" "$preset" "$zvm_version" "$extra_settings" "$extra_optimizer_settings")
    gas_reporter_settings=$(eth_gas_reporter_settings "$preset")

    {
        echo "require('eth-gas-reporter');"
        echo "module.exports['mocha'] = {"
        echo "    reporter: 'eth-gas-reporter',"
        echo "    reporterOptions: ${gas_reporter_settings}"
        echo "};"

        echo "module.exports['compilers'] = ${compiler_settings};"
    } >> "$config_file"
}

function name_hardhat_default_export
{
    local config_file="$1"
    local import="import {HardhatUserConfig} from 'hardhat/types/config';"
    local config="const config: HardhatUserConfig = {"
    sed -i "s|^\s*export\s*default\s*{|${import}\n${config}|g" "$config_file"
    echo "export default config;" >> "$config_file"
}

function force_hardhat_timeout
{
    local config_file="$1"
    local config_var_name="$2"
    local new_timeout="$3"

    printLog "Configuring Hardhat..."
    echo "-------------------------------------"
    echo "Timeout: ${new_timeout}"
    echo "-------------------------------------"

    if [[ $config_file == *\.js ]]; then
        [[ $config_var_name == "" ]] || assertFail
        echo "module.exports.mocha = module.exports.mocha || {timeout: ${new_timeout}}"
        echo "module.exports.mocha.timeout =  ${new_timeout}"
    else
        [[ $config_file == *\.ts ]] || assertFail
        [[ $config_var_name != "" ]] || assertFail
        echo "${config_var_name}.mocha = ${config_var_name}.mocha ?? {timeout: ${new_timeout}};"
        echo "${config_var_name}.mocha!.timeout = ${new_timeout}"
    fi >> "$config_file"
}

function force_hardhat_compiler_binary
{
    local config_file="$1"
    local binary_type="$2"
    local hypc_path="$3"

    printLog "Configuring Hardhat..."
    echo "-------------------------------------"
    echo "Config file: ${config_file}"
    echo "Binary type: ${binary_type}"
    echo "Compiler path: ${hypc_path}"

    local language="${config_file##*.}"
    hardhat_hypc_build_subtask "$HYPCVERSION_SHORT" "$HYPCVERSION" "$binary_type" "$hypc_path" "$language" >> "$config_file"
}

function force_hardhat_unlimited_contract_size
{
    local config_file="$1"
    local config_var_name="$2"

    printLog "Configuring Hardhat..."
    echo "-------------------------------------"
    echo "Allow unlimited contract size: true"
    echo "-------------------------------------"

    if [[ $config_file == *\.js ]]; then
        [[ $config_var_name == "" ]] || assertFail
        echo "module.exports.networks.hardhat = module.exports.networks.hardhat || {allowUnlimitedContractSize: undefined}"
        echo "module.exports.networks.hardhat.allowUnlimitedContractSize = true"
    else
        [[ $config_file == *\.ts ]] || assertFail
        [[ $config_var_name != "" ]] || assertFail
        echo "${config_var_name}.networks!.hardhat = ${config_var_name}.networks!.hardhat ?? {allowUnlimitedContractSize: undefined};"
        echo "${config_var_name}.networks!.hardhat!.allowUnlimitedContractSize = true"
    fi >> "$config_file"
}

function force_hardhat_compiler_settings
{
    local config_file="$1"
    local preset="$2"
    local config_var_name="$3"
    local zvm_version="${4:-"$CURRENT_ZVM_VERSION"}"
    local extra_settings="$5"
    local extra_optimizer_settings="$6"

    printLog "Configuring Hardhat..."
    echo "-------------------------------------"
    echo "Config file: ${config_file}"
    echo "Settings preset: ${preset}"
    echo "Settings: $(settings_from_preset "$preset" "$zvm_version" "$extra_settings" "$extra_optimizer_settings")"
    echo "ZVM version: ${zvm_version}"
    echo "Compiler version: ${HYPCVERSION_SHORT}"
    echo "Compiler version (full): ${HYPCVERSION}"
    echo "-------------------------------------"

    local compiler_settings gas_reporter_settings
    compiler_settings=$(hardhat_compiler_settings "$HYPCVERSION_SHORT" "$preset" "$zvm_version" "$extra_settings" "$extra_optimizer_settings")
    gas_reporter_settings=$(eth_gas_reporter_settings "$preset")
    if [[ $config_file == *\.js ]]; then
        [[ $config_var_name == "" ]] || assertFail
        echo "require('hardhat-gas-reporter');"
        echo "module.exports.gasReporter = ${gas_reporter_settings};"
        echo "module.exports.hyperion = ${compiler_settings};"
    else
        [[ $config_file == *\.ts ]] || assertFail
        [[ $config_var_name != "" ]] || assertFail
        echo 'import "hardhat-gas-reporter";'
        echo "${config_var_name}.gasReporter = ${gas_reporter_settings};"
        echo "${config_var_name}.hyperion = {compilers: [${compiler_settings}]};"
    fi >> "$config_file"
}

function truffle_verify_compiler_version
{
    local hypc_version="$1"
    local full_hypc_version="$2"

    printLog "Verify that the correct version (${hypc_version}/${full_hypc_version}) of the compiler was used to compile the contracts..."
    grep "$full_hypc_version" --recursive --quiet build/contracts || fail "Wrong compiler version detected."
}

function hardhat_verify_compiler_version
{
    local hypc_version="$1"
    local full_hypc_version="$2"

    printLog "Verify that the correct version (${hypc_version}/${full_hypc_version}) of the compiler was used to compile the contracts..."
    local build_info_files
    build_info_files=$(find . -path '*artifacts/build-info/*.json')
    for build_info_file in $build_info_files; do
        grep '"hypcVersion":[[:blank:]]*"'"${hypc_version}"'"' --quiet "$build_info_file" || fail "Wrong compiler version detected in ${build_info_file}."
        grep '"hypcLongVersion":[[:blank:]]*"'"${full_hypc_version}"'"' --quiet "$build_info_file" || fail "Wrong compiler version detected in ${build_info_file}."
    done
}

function truffle_clean
{
    rm -rf build/
}

function hardhat_clean
{
    rm -rf build/ artifacts/ cache/
}

function settings_from_preset
{
    local preset="$1"
    local zvm_version="$2"
    local extra_settings="$3"
    local extra_optimizer_settings="$4"

    [[ " ${AVAILABLE_PRESETS[*]} " == *" $preset "* ]] || assertFail

    [[ $extra_settings == "" ]] || extra_settings="${extra_settings}, "
    [[ $extra_optimizer_settings == "" ]] || extra_optimizer_settings="${extra_optimizer_settings}, "

    case "$preset" in
        # NOTE: Remember to update `parallelism` of `t_ems_ext` job in CI config if you add/remove presets
        legacy-no-optimize)       echo "{${extra_settings}zvmVersion: '${zvm_version}', viaIR: false, optimizer: {${extra_optimizer_settings}enabled: false}}" ;;
        ir-no-optimize)           echo "{${extra_settings}zvmVersion: '${zvm_version}', viaIR: true,  optimizer: {${extra_optimizer_settings}enabled: false}}" ;;
        legacy-optimize-zvm-only) echo "{${extra_settings}zvmVersion: '${zvm_version}', viaIR: false, optimizer: {${extra_optimizer_settings}enabled: true, details: {yul: false}}}" ;;
        ir-optimize-zvm-only)     echo "{${extra_settings}zvmVersion: '${zvm_version}', viaIR: true,  optimizer: {${extra_optimizer_settings}enabled: true, details: {yul: false}}}" ;;
        legacy-optimize-zvm+yul)  echo "{${extra_settings}zvmVersion: '${zvm_version}', viaIR: false, optimizer: {${extra_optimizer_settings}enabled: true, details: {yul: true}}}" ;;
        ir-optimize-zvm+yul)      echo "{${extra_settings}zvmVersion: '${zvm_version}', viaIR: true,  optimizer: {${extra_optimizer_settings}enabled: true, details: {yul: true}}}" ;;
        *)
            fail "Unknown settings preset: '${preset}'."
            ;;
    esac
}

function replace_global_hypc
{
    local hypc_path="$1"

    [[ ! -e hypc ]] || fail "A file named 'hypc' already exists in '${PWD}'."

    ln -s "$hypc_path" hypc
    export PATH="$PWD:$PATH"
}

function eth_gas_reporter_settings
{
    local preset="$1"

    echo "{"
    echo "    enabled: true,"
    echo "    gasPrice: 1,"                           # Gas price does not matter to us at all. Set to whatever to avoid API call.
    echo "    noColors: true,"
    echo "    showTimeSpent: false,"                  # We're not interested in test timing
    echo "    onlyCalledMethods: true,"               # Exclude entries with no gas for shorter report
    echo "    showMethodSig: true,"                   # Should make diffs more stable if there are overloaded functions
    echo "    outputFile: \"$(gas_report_path "$preset")\""
    echo "}"
}

function truffle_compiler_settings
{
    local hypc_path="$1"
    local preset="$2"
    local zvm_version="$3"
    local extra_settings="$4"
    local extra_optimizer_settings="$5"

    echo "{"
    echo "    hypc: {"
    echo "        version: \"${hypc_path}\","
    echo "        settings: $(settings_from_preset "$preset" "$zvm_version" "$extra_settings" "$extra_optimizer_settings")"
    echo "    }"
    echo "}"
}

function hardhat_hypc_build_subtask {
    local hypc_version="$1"
    local full_hypc_version="$2"
    local binary_type="$3"
    local hypc_path="$4"
    local language="$5"

    [[ $binary_type == native || $binary_type == hypcjs ]] || assertFail

    [[ $binary_type == native ]] && local is_hypcjs=false
    [[ $binary_type == hypcjs ]] && local is_hypcjs=true

    if [[ $language == js ]]; then
        echo "const {TASK_COMPILE_HYPERION_GET_HYPC_BUILD} = require('hardhat/builtin-tasks/task-names');"
        echo "const assert = require('assert');"
        echo
        echo "subtask(TASK_COMPILE_HYPERION_GET_HYPC_BUILD, async (args, hre, runSuper) => {"
    else
        [[ $language == ts ]] || assertFail
        echo "import {TASK_COMPILE_HYPERION_GET_HYPC_BUILD} from 'hardhat/builtin-tasks/task-names';"
        echo "import assert = require('assert');"
        echo "import {subtask} from 'hardhat/config';"
        echo
        echo "subtask(TASK_COMPILE_HYPERION_GET_HYPC_BUILD, async (args: any, _hre: any, _runSuper: any) => {"
    fi

    echo "    assert(args.hypcVersion == '${hypc_version}', 'Unexpected hypc version: ' + args.hypcVersion)"
    echo "    return {"
    echo "        compilerPath: '$(realpath "$hypc_path")',"
    echo "        isHypcJs: ${is_hypcjs},"
    echo "        version: args.hypcVersion,"
    echo "        longVersion: '${full_hypc_version}'"
    echo "    }"
    echo "})"
}

function hardhat_compiler_settings {
    local hypc_version="$1"
    local preset="$2"
    local zvm_version="$3"
    local extra_settings="$4"
    local extra_optimizer_settings="$5"

    echo "{"
    echo "    version: '${hypc_version}',"
    echo "    settings: $(settings_from_preset "$preset" "$zvm_version" "$extra_settings" "$extra_optimizer_settings")"
    echo "}"
}

function compile_and_run_test
{
    local compile_fn="$1"
    local test_fn="$2"
    local verify_fn="$3"
    local preset="$4"
    local compile_only_presets="$5"

    [[ $preset != *" "* ]] || assertFail "Preset names must not contain spaces."

    printLog "Running compile function..."
    time $compile_fn
    $verify_fn "$HYPCVERSION_SHORT" "$HYPCVERSION"

    if [[ "$COMPILE_ONLY" == 1 || " $compile_only_presets " == *" $preset "* ]]; then
        printLog "Skipping test function..."
    else
        printLog "Running test function..."
        $test_fn
    fi
}

function truffle_run_test
{
    local config_file="$1"
    local binary_type="$2"
    local hypc_path="$3"
    local preset="$4"
    local compile_only_presets="$5"
    local compile_fn="$6"
    local test_fn="$7"
    local extra_settings="$8"
    local extra_optimizer_settings="$9"

    truffle_clean
    force_truffle_compiler_settings "$config_file" "$binary_type" "$hypc_path" "$preset" "$CURRENT_ZVM_VERSION" "$extra_settings" "$extra_optimizer_settings"
    compile_and_run_test compile_fn test_fn truffle_verify_compiler_version "$preset" "$compile_only_presets"
}

function hardhat_run_test
{
    local config_file="$1"
    local preset="$2"
    local compile_only_presets="$3"
    local compile_fn="$4"
    local test_fn="$5"
    local config_var_name="$6"
    local extra_settings="$7"
    local extra_optimizer_settings="$8"

    hardhat_clean
    force_hardhat_compiler_settings "$config_file" "$preset" "$config_var_name" "$CURRENT_ZVM_VERSION" "$extra_settings" "$extra_optimizer_settings"
    compile_and_run_test compile_fn test_fn hardhat_verify_compiler_version "$preset" "$compile_only_presets"
}

function external_test
{
    local name="$1"
    local main_fn="$2"

    printTask "Testing $name..."
    echo "==========================="
    DIR=$(mktemp -d -t "ext-test-${name}-XXXXXX")
    (
        [[ "$main_fn" != "" ]] || fail "Test main function not defined."
        $main_fn
    )
    rm -rf "$DIR"
    echo "Done."
}

function gas_report_path
{
    local preset="$1"

    echo "${DIR}/gas-report-${preset}.rst"
}

function gas_report_to_json
{
    cat - | "${REPO_ROOT}/scripts/externalTests/parse_zond_gas_report.py" | jq '{gas: .}'
}

function detect_hardhat_artifact_dir
{
    if [[ -e build/ && -e artifacts/ ]]; then
        fail "Cannot determine Hardhat artifact location. Both build/ and artifacts/ exist"
    elif [[ -e build/ ]]; then
        echo -n build/artifacts
    elif [[ -e artifacts/ ]]; then
        echo -n artifacts
    else
        fail "Hardhat build artifacts not found."
    fi
}

function bytecode_size_json_from_truffle_artifacts
{
    # NOTE: The output of this function is a series of concatenated JSON dicts rather than a list.

    for artifact in build/contracts/*.json; do
        if [[ $(jq '. | has("unlinked_binary")' "$artifact") == false ]]; then
            # Each artifact represents compilation output for a single contract. Some top-level keys contain
            # bits of Standard JSON output while others are generated by Truffle. Process it into a dict
            # of the form `{"<file>": {"<contract>": <size>}}`.
            # NOTE: The `bytecode` field starts with 0x, which is why we subtract 1 from size.
            jq '{
                (.ast.absolutePath): {
                    (.contractName): (.bytecode | length / 2 - 1)
                }
            }' "$artifact"
        fi
    done
}

function bytecode_size_json_from_hardhat_artifacts
{
    # NOTE: The output of this function is a series of concatenated JSON dicts rather than a list.

    for artifact in "$(detect_hardhat_artifact_dir)"/build-info/*.json; do
        # Each artifact contains Standard JSON output under the `output` key.
        # Process it into a dict of the form `{"<file>": {"<contract>": <size>}}`,
        # Note that one Hardhat artifact often represents multiple input files.
        jq '.output.contracts | to_entries[] | {
            "\(.key)": .value | to_entries[] | {
                "\(.key)": (.value.zvm.bytecode.object | length / 2)
            }
        }' "$artifact"
    done
}

function combine_artifact_json
{
    # Combine all dicts into a list with `jq --slurp` and then use `reduce` to merge them into one
    # big dict with keys of the form `"<file>:<contract>"`. Then run jq again to filter out items
    # with zero size and put the rest under under a top-level `bytecode_size` key. Also add another
    # key with total bytecode size.
    # NOTE: The extra inner `bytecode_size` key is there only to make diffs more readable.
    cat - |
        jq --slurp 'reduce (.[] | to_entries[]) as {$key, $value} ({}; . + {
            ($key + ":" + ($value | to_entries[].key)): {
                bytecode_size: $value | to_entries[].value
            }
        })' |
        jq --indent 4 --sort-keys '{
            bytecode_size: [. | to_entries[] | select(.value.bytecode_size > 0)] | from_entries,
            total_bytecode_size: (reduce (. | to_entries[]) as {$key, $value} (0; . + $value.bytecode_size))
        }'
}

function project_info_json
{
    local project_url="$1"

    echo "{"
    echo "    \"project\": {"
    # NOTE: Given that we clone with `--depth 1`, we'll only get useful output out of `git describe`
    # if we directly check out a tag. Still better than nothing.
    echo "        \"version\": \"$(git describe --always)\","
    echo "        \"commit\": \"$(git rev-parse HEAD)\","
    echo "        \"url\": \"${project_url}\""
    echo "    }"
    echo "}"
}

function store_benchmark_report
{
    local framework="$1"
    local project_name="$2"
    local project_url="$3"
    local preset="$4"

    [[ $framework == truffle || $framework == hardhat ]] || assertFail
    [[ " ${AVAILABLE_PRESETS[*]} " == *" $preset "* ]] || assertFail

    local report_dir="${REPO_ROOT}/reports/externalTests"
    local output_file="${report_dir}/benchmark-${project_name}-${preset}.json"
    mkdir -p "$report_dir"

    {
        if [[ -e $(gas_report_path "$preset") ]]; then
            gas_report_to_json < "$(gas_report_path "$preset")"
        fi

        "bytecode_size_json_from_${framework}_artifacts" | combine_artifact_json
        project_info_json "$project_url"
    } | jq --slurp "{\"${project_name}\": {\"${preset}\": add}}" --indent 4 --sort-keys > "$output_file"
}
