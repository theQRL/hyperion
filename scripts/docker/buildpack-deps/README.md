# Build dependency images

GitHub Actions uses the pinned Linux and Emscripten images declared in
`.github/workflows/linux.yml` and `.github/workflows/ci.yml`. The Emscripten
image digest must also match `scripts/build_emscripten.sh` for local builds.

The Dockerfiles in this directory document how to rebuild the dependency
images. Image publishing is a separate maintainer operation; this repository
does not currently contain an automatic registry publishing workflow.

After rebuilding an image, validate it with the corresponding
`scripts/ci/buildpack-deps_test_*.sh` entry point and update the pinned digest.
Those entry points delegate to the same scripts used by Actions. Linux jobs
build the pinned QRVM test engine separately, because the older engine bundled
in the dependency images does not match the current address and word sizes.

Historical Arch Linux, coverage-upload and OSS-Fuzz jobs are not part of the
new workflow. The OSS-Fuzz tooling remains available for manual use.
