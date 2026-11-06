# Build dependency images

GitHub Actions uses the pinned Linux and Emscripten images declared in
`.github/workflows/linux.yml` and `.github/workflows/ci.yml`. The Emscripten
image digest must also match `scripts/build_emscripten.sh` for local builds.

The AMD64 Linux jobs use Solidity's `solbuildpackpusher/solidity-buildpack-deps`
images, pinned by digest and built from Solidity's own Dockerfiles. They include
Boost and Z3. The ARM64 jobs use Docker's official `buildpack-deps:jammy` image,
also pinned by digest. For ARM64, `linux.yml` installs Boost and the build tools
with apt and builds Z3 with `scripts/ci/build_z3.sh`. This repository has no
Dockerfiles for any of the Linux images. None of them contains a QRVM engine, so
every Linux job builds the pinned QRVM test engine itself.

The Dockerfiles in this directory document how to rebuild the Emscripten image
and the OSS-Fuzz image. Image publishing is a separate maintainer operation;
this repository does not currently contain an automatic registry publishing
workflow. After rebuilding an image, validate it with the corresponding
`scripts/ci/buildpack-deps_test_*.sh` entry point and update the pinned digest.
The Emscripten entry point delegates to the same script used by Actions.

Historical Arch Linux, coverage-upload and OSS-Fuzz jobs are not part of the
new workflow. The OSS-Fuzz tooling remains available for manual use.
