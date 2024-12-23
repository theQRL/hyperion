#!/usr/bin/env bash

# ------------------------------------------------------------------------------
# Determines versions of all release binaries from hypc-bin repo modified in the
# specified commit range, finds all release binaries from one, selected platform
# that match these versions and uses them to produce bytecode reports.
#
# The script is meant to be backwards-compatible with older versions of the
# compiler. Reports are generated via the CLI interface (rather than Standard
# JSON) in case of native binaries because the JSON interface was not available
# in very old versions.
#
# The script creates one report per binary and file names follow the pattern
# 'report-<binary name>.txt'.
#
# Usage:
#    <script name>.sh <PLATFORM> <BASE_REF> <TOP_REF> <HYPC_BIN_DIR> <HYPERION_DIR>
#
# PLATFORM: Platform name, corresponding the one of the top-level directories
#     in hypc-bin.
# BASE_REF..TOP_REF: Commit range in the hypc-bin repository to search for
#     modified binaries.
# HYPC_BIN_DIR: Directory containing a checkout of the ethereum/hypc-bin
#    repository with full history. Must be an absolute path.
# HYPERION_DIR: Directory containing a checkout of the ethereum/hyperion
#    repository with full history. Bytecode report will be generated using
#    scripts from the currently checked out revision. Must be an absolute path.
#
# Example:
#    <script name>.sh linux-amd64 gh-pages pr-branch "$PWD/hypc-bin" "$PWD/hyperion"
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
# (c) 2020 hyperion contributors.
#------------------------------------------------------------------------------

# FIXME: Can't use set -u because the old Bash on macOS treats empty arrays as unbound variables
set -eo pipefail

function die
{
    # shellcheck disable=SC2059
    >&2 printf "ERROR: $1\n" "${@:2}"
    exit 1
}

function get_reported_hypc_version
{
    local hypc_binary="$1"

    local version_banner; version_banner=$("$hypc_binary" --version)

    if [[ ! $(echo "$version_banner" | head -n 1) =~ ^hypc,.*$ ]]; then
        die "%s\nFULL OUTPUT:\n" "Invalid format of --version output" "$version_banner"
    fi

    echo "$version_banner" | tail -n 1 | sed -n -E 's/^Version: (.*)$/\1/p'
}

function validate_reported_version
{
    local reported_version="$1"
    local expected_version_and_commit="$2"

    if [[ $reported_version =~ [\-.]mod\. ]]; then
        die "Version '%s' reported by the '%s' binary indicates that it was built from modified source." "$reported_version" "$expected_version_and_commit"
    fi

    local actual_version_and_commit; actual_version_and_commit=$(
        echo "$reported_version" |
        sed -E 's/^[[:space:]]*([0-9.]+\+commit\.[0-9a-f]+)\..+\..+$/\1/'
    )

    if [[ $actual_version_and_commit != "$expected_version_and_commit" ]]; then
        die "Binary identifies itself as version '%s' which does not match '%s' present in its name." "$actual_version_and_commit" "$expected_version_and_commit"
    fi

    echo "Binary for version ${expected_version_and_commit} reports correct version."
}

(( $# == 5 )) || die "ERROR: Not enough arguments."

platform="$1"
base_ref="$2"
top_ref="$3"
hypc_bin_dir="$4"
hyperion_dir="$5"

report_dir="$PWD"
tmp_dir=$(mktemp -d -t bytecode-reports-XXXXXX)
hypcjs_dir="$tmp_dir/hypcjs"
script_dir="$hyperion_dir/scripts"

# Set locale to C to prevent it from affecting glob sort order.
export LC_ALL=C

cd "$tmp_dir"

git clone https://github.com/theQRL/hypc-js.git "$hypcjs_dir"
cd "$hypcjs_dir"
npm install
npm run build

cd "${hypc_bin_dir}/${platform}/"
echo "Commit range: ${base_ref}..${top_ref}"

modified_release_versions=$(
    git diff --name-only "${base_ref}" "${top_ref}" |
    sed -n -E 's/^[^\/]+\/(hypc|hypjson)-[0-9a-zA-Z-]+-v([0-9.]+)\+commit\.[0-9a-f]+(.[^.]+)?$/\2/p' |
    sort -V |
    uniq
)
echo "Release versions modified in the commit range:"
echo "$modified_release_versions"

# NOTE: We want perform the check when the hypjson-* files in bin/ and wasm/ are modified too
# because in that case the symlinks in emscripten-wasm32/ and emscripten-asmjs/ might remain
# unchanged but were're assuming that these directories are never directly used as a platform name.
[[ $platform != bin && $platform != wasm ]] || die "Invalid platform name."

platform_binaries="$(git ls-files "hypc-${platform}-v*+commit.*" | sort -V)"

for binary_name in $platform_binaries; do
    hyperion_version_and_commit=$(echo "$binary_name" | sed -n -E 's/^hypc-'"${platform}"'-v([0-9.]+\+commit\.[0-9a-f]+).*$/\1/p')
    hyperion_version=$(echo "$hyperion_version_and_commit" | sed -n -E 's/^([0-9.]+).*$/\1/p')

    if echo "$modified_release_versions" | grep -x "$hyperion_version"; then
        echo "Binary ${binary_name} (version ${hyperion_version}) matches one of the modified versions."

        work_dir="${tmp_dir}/${binary_name}"
        mkdir "$work_dir"
        cd "$work_dir"

        # While bytecode scripts come from the latest compiler, the test files should come from
        # the Hyperion version we're running them against to avoid errors due to breaking syntax changes.
        git clone --branch "v${hyperion_version}" "$hyperion_dir" "${work_dir}/hyperion/"
        "${script_dir}/isolate_tests.py" "${work_dir}/hyperion/test/"

        if [[ $platform == emscripten-wasm32 ]] || [[ $platform == emscripten-asmjs ]]; then
            ln -sf "${hypc_bin_dir}/${platform}/${binary_name}" "${hypcjs_dir}/hypjson.js"
            ln -sf "${hypc_bin_dir}/${platform}/${binary_name}" "${hypcjs_dir}/dist/hypjson.js"
            npm install "${hypcjs_dir}/dist"
            cp "${script_dir}/bytecodecompare/prepare_report.js" prepare_report.js

            validate_reported_version \
                "$(node_modules/hypc/hypc.js --version)" \
                "$hyperion_version_and_commit"

            # shellcheck disable=SC2035
            ./prepare_report.js --strip-smt-pragmas *.hyp > "${report_dir}/report-${binary_name}.txt"
        else
            yul_optimizer_flags=()
            if [[ $hyperion_version == 0.6.0 ]] || [[ $hyperion_version == 0.6.1 ]]; then
                yul_optimizer_flags+=(--force-no-optimize-yul)
            fi

            validate_reported_version \
                "$(get_reported_hypc_version "${hypc_bin_dir}/${platform}/${binary_name}")" \
                "$hyperion_version_and_commit"

            "${script_dir}/bytecodecompare/prepare_report.py" "${hypc_bin_dir}/${platform}/${binary_name}" \
                --interface cli \
                --smt-use strip-pragmas \
                --report-file "${report_dir}/report-${binary_name}.txt" \
                "${yul_optimizer_flags[@]}"
        fi

        rm -r "${work_dir}/hyperion/"
    else
        echo "Binary ${binary_name} (version ${hyperion_version}) does not match any modified version. Skipping."
    fi
done

rm -r "$tmp_dir"
