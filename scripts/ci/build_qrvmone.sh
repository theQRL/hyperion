#!/usr/bin/env bash
# deps/qrvmone is checked out at the workflow's pinned revision.
set -euo pipefail
REPO_ROOT=$(cd "$(dirname "$0")/../.." && pwd)
cd "$REPO_ROOT"
cmake_args=()
# Prefer the runner's platform; recent Git Bash also identifies as cygwin.
case "${RUNNER_OS:-$OSTYPE}" in
    macOS|darwin*) library=libqrvmone.dylib ;;
    Windows|msys*|cygwin*|mingw*)
        library=qrvmone.dll
        # The pinned engine's Hunter release does not recognize MSVC 19.4x.
        cmake_args+=("-DCMAKE_MODULE_PATH=$(cygpath -m "$REPO_ROOT/scripts/ci/cmake")")
        ;;
    *) library=libqrvmone.so ;;
esac
# Bash 3.2 treats an empty array as unset under nounset.
cmake -S deps/qrvmone -B deps/qrvmone/build \
    -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON \
    -DQRVMONE_TESTING=OFF -DQRVMONE_X86_64_ARCH_LEVEL=1 ${cmake_args[@]+"${cmake_args[@]}"}
cmake --build deps/qrvmone/build --config Release --parallel "${BUILD_JOBS:-2}"
library_path=$(find "$REPO_ROOT/deps/qrvmone/build" -name "$library" -print -quit)
[[ -n $library_path ]] || { echo "Missing $library" >&2; exit 1; }
if [[ $library == qrvmone.dll ]]; then
    library_path=$(cygpath -w "$library_path")
fi
printf 'QRL_QRVMONE=%s\n' "$library_path" >> "$GITHUB_ENV"
