#!/usr/bin/env bash

#------------------------------------------------------------------------------
# Bash script to run optimizer performance tests.
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
# (c) 2022 hyperion contributors.
#------------------------------------------------------------------------------

set -euo pipefail

REPO_ROOT=$(cd "$(dirname "$0")/../../" && pwd)
HYPERION_BUILD_DIR=${HYPERION_BUILD_DIR:-${REPO_ROOT}/build}

output_dir=$(mktemp -d -t hypc-benchmark-XXXXXX)
result_legacy_file="${output_dir}/benchmark-legacy.txt"
result_via_ir_file="${output_dir}/benchmark-via-ir.txt"
warnings_and_errors_file="${output_dir}/benchmark-warn-err.txt"

function cleanup() {
    rm -r "${output_dir}"
    exit
}

trap cleanup SIGINT SIGTERM

hypc="${HYPERION_BUILD_DIR}/hypc/hypc"
benchmarks_dir="${REPO_ROOT}/test/benchmarks"
benchmarks=("chains.hyp" "OptimizorClub.hyp" "verifier.hyp")
time_bin_path=$(type -P time)

for input_file in "${benchmarks[@]}"
do
    input_path="${benchmarks_dir}/${input_file}"

    hypc_command_legacy=("${hypc}" --optimize --bin --color "${input_path}")
    hypc_command_via_ir=("${hypc}" --via-ir --optimize --bin --color "${input_path}")

    # Legacy can fail.
    "${time_bin_path}" --output "${result_legacy_file}" --format "%e" "${hypc_command_legacy[@]}" >/dev/null 2>>"${warnings_and_errors_file}"
    "${time_bin_path}" --output "${result_via_ir_file}" --format "%e" "${hypc_command_via_ir[@]}" >/dev/null 2>>"${warnings_and_errors_file}"

    time_legacy=$(<"${result_legacy_file}")
    time_via_ir=$(<"${result_via_ir_file}")

    echo "======================================================="
    echo "            ${input_file}"
    echo "-------------------------------------------------------"
    echo "legacy pipeline took ${time_legacy} seconds to execute."
    echo "via-ir pipeline took ${time_via_ir} seconds to execute."
    echo "======================================================="
done

echo
echo "======================================================="
echo "Warnings and errors generated during run:"
echo "======================================================="
echo "$(<"${warnings_and_errors_file}")"

cleanup

