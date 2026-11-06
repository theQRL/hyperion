# Build dependency images

GitHub Actions uses the pinned Linux and Emscripten images declared in
`.github/workflows/linux.yml` and `.github/workflows/ci.yml`. The Emscripten
image digest must also match `scripts/build_emscripten.sh` for local builds.

The Linux images are Solidity's `solbuildpackpusher/solidity-buildpack-deps`
images, pinned by digest and built from Solidity's own Dockerfiles. This
repository has no Dockerfiles for them. They contain no QRVM engine, so Linux
jobs build the pinned QRVM test engine themselves.

The Dockerfiles in this directory document how to rebuild the Emscripten image
and the OSS-Fuzz image. Image publishing is a separate maintainer operation;
this repository does not currently contain an automatic registry publishing
workflow. After rebuilding an image, validate it with the corresponding
`scripts/ci/buildpack-deps_test_*.sh` entry point and update the pinned digest.
The Emscripten entry point delegates to the same script used by Actions.

Historical Arch Linux, coverage-upload and OSS-Fuzz jobs are not part of the
new workflow. The OSS-Fuzz tooling remains available for manual use.
