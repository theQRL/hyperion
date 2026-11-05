#!/usr/bin/env bash
set -euo pipefail
REPO_ROOT=$(cd "$(dirname "$0")/../.." && pwd)
brew install bash coreutils diffutils gnu-sed grep jq
for package in coreutils gnu-sed grep; do
    printf '%s/libexec/gnubin\n' "$(brew --prefix "$package")" >> "$GITHUB_PATH"
done

# Pin Boost: upstream changes to Filesystem can break this compiler's API usage. hypc-macos is a
# universal binary, so build both architectures, for the deployment target set in CMakeLists.txt.
universal_boost() {
    local library arch
    for library in filesystem program_options unit_test_framework; do
        # One architecture per call: Xcode 27's lipo rejects a list after -verify_arch.
        for arch in arm64 x86_64; do
            lipo "$REPO_ROOT/deps/boost/lib/libboost_$library.a" -verify_arch "$arch" || return 1
        done
    done
}
if ! universal_boost 2>/dev/null; then
    rm -rf "$REPO_ROOT/deps/boost"
    mkdir -p "$REPO_ROOT/deps"
    cd "$REPO_ROOT/deps"
    curl --fail --location --retry 3 -o boost.zip https://archives.boost.io/release/1.83.0/source/boost_1_83_0.zip
    echo 'c86bd9d9eef795b4b0d3802279419fde5221922805b073b9bd822edecb1ca28e  boost.zip' | shasum -a 256 -c -
    rm -rf boost_1_83_0
    unzip -q boost.zip
    cd boost_1_83_0
    ./bootstrap.sh
    ./b2 -j"${BUILD_JOBS:-2}" link=static variant=release threading=multi \
        architecture=arm+x86 address-model=64 cxxflags=-mmacosx-version-min=11.0 \
        --with-filesystem --with-system --with-program_options --with-test \
        --prefix="$REPO_ROOT/deps/boost" install
    universal_boost
fi
