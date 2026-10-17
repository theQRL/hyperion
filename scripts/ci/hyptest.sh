#!/usr/bin/env bash
# Run the compiler tests locally or in a CI matrix, retaining every worker's result.
set -euo pipefail

REPO_ROOT=$(cd "$(dirname "$0")/../.." && pwd)
cd "$REPO_ROOT"
binary=${HYPTEST_BINARY:-${HYPERION_BUILD_DIR:-build}/test/hyptest}
workers=${TEST_JOBS:-2}
shards=${CI_NODE_TOTAL:-1}
shard=${CI_NODE_INDEX:-0}
if ! [[ $workers =~ ^[1-9][0-9]*$ && $shards =~ ^[1-9][0-9]*$ && $shard =~ ^[0-9]+$ ]] || (( shard >= shards )); then
    echo 'Invalid test worker or shard configuration.' >&2
    exit 1
fi
IFS=' ' read -r -a boost_args <<< "${BOOST_TEST_ARGS:-}"
IFS=' ' read -r -a test_args <<< "${HYPTEST_FLAGS:-}"
test_args+=(--testpath "$REPO_ROOT/test" --qrvm-version="${QRVM:-zond}")
[[ ${OPTIMIZE:-0} != 1 ]] || test_args+=(--optimize)
[[ ${ABI_ENCODER_V1:-0} != 1 ]] || test_args+=(--abiencoderv1)
mkdir -p test_results
# Windows stack size is set by the MSVC linker in QRLCompilerSettings.cmake.
if [[ $OSTYPE != msys* ]]; then
    ulimit -s 16384
fi

pids=()
for ((worker=0; worker<workers; worker++)); do
    batch=$((workers * shard + worker))
    report="test_results/${QRVM:-zond}-opt${OPTIMIZE:-0}-abi${ABI_ENCODER_V1:-0}-${batch}.xml"
    "$binary" --color_output=no --report_level=short \
        "--logger=JUNIT,error,$report" --logger=HRF,error,stdout "${boost_args[@]}" \
        -- "${test_args[@]}" --batches "$((workers * shards))" --selected-batch "$batch" &
    pids+=("$!")
done
status=0
for pid in "${pids[@]}"; do
    wait "$pid" || status=1
done
exit "$status"
