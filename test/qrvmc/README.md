# QRVMC

This is an import of [QRVMC](https://github.com/theQRL/qrvmc) version [3.0.0](https://github.com/theQRL/qrvmc/releases/tag/v3.0.0)
(`include/qrvmc/` and `lib/loader/loader.c` at commit `fb3a10b4b14777e49aeabac3b576c7754d5aff3c`), the QRVMC that the
QRVM test engine [qrvmone 0.12.1](https://github.com/theQRL/qrvmone/releases/tag/v0.12.1) is built with. Update this
import and the qrvmone pin in `.github/workflows/ci.yml` and `.github/workflows/linux.yml` together: the loader rejects
a test engine built against a different QRVMC ABI version.

Local changes:

- `MockedAccount.storage` is a `map` instead of an `unordered_map`, as ordering is important for fuzzing.
- `MockedAccount.nonce` is `uint64_t` to match go-qrl's account nonce range.
- A spelling mistake in a `qrvmc.h` comment is fixed for the codespell check.
