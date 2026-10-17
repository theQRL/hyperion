#!/usr/bin/env bash
set -euo pipefail
REPO_ROOT=$(cd "$(dirname "$0")/../.." && pwd)
brew install bash coreutils diffutils gnu-sed grep jq
for package in coreutils gnu-sed grep; do
    printf '%s/libexec/gnubin\n' "$(brew --prefix "$package")" >> "$GITHUB_PATH"
done

# Pin Boost: upstream changes to Filesystem can break this compiler's API usage.
if [[ ! -f $REPO_ROOT/deps/boost/lib/libboost_filesystem.a ]]; then
    mkdir -p "$REPO_ROOT/deps"
    cd "$REPO_ROOT/deps"
    curl --fail --location --retry 3 -o boost.zip https://archives.boost.io/release/1.83.0/source/boost_1_83_0.zip
    echo 'c86bd9d9eef795b4b0d3802279419fde5221922805b073b9bd822edecb1ca28e  boost.zip' | shasum -a 256 -c -
    unzip -q boost.zip
    cd boost_1_83_0
    ./bootstrap.sh
    ./b2 -j"${BUILD_JOBS:-2}" link=static variant=release threading=multi \
        --with-filesystem --with-system --with-program_options --with-test \
        --prefix="$REPO_ROOT/deps/boost" install
fi
