# QRVMC

This is an import of [QRVMC](https://github.com/theQRL/qrvmc) (`include/qrvmc/` and `lib/loader/loader.c`) at commit
`03b28669a56109eca459d63ad8d247e5428f0298`: version [3.0.0](https://github.com/theQRL/qrvmc/releases/tag/v3.0.0) with the
Host methods of the transient storage ([EIP-1153](https://eips.ethereum.org/EIPS/eip-1153)), `get_transient_storage` and
`set_transient_storage`, which a QRVM test engine with `TLOAD` and `TSTORE` calls. Update this import and the qrvmone pin
in `.github/workflows/ci.yml` and `.github/workflows/linux.yml` together. The loader rejects a test engine built against
a different QRVMC ABI version, but the ABI version is 3 both before and after the two methods were added, so it does not
notice an engine that expects them from a Host built without them.

The commit also has the `MCOPY` instruction ([EIP-5656](https://eips.ethereum.org/EIPS/eip-5656)), which changes
nothing in the imported files but a comment: the instruction works on the memory of the engine and calls no Host method.
The compiler generates `MCOPY`, so every semantic test of a contract that copies a range of memory needs a QRVM test
engine that has it: about one test in nine fails on an engine without it.

It has the `CLZ` instruction ([EIP-7939](https://eips.ethereum.org/EIPS/eip-7939)) as well, which again changes
nothing in the imported files but that comment. The compiler does not generate `CLZ`: only the tests of the `clz`
builtin execute it, and they fail on an engine without it, to which `0x1e` is an undefined instruction.

Local changes:

- `MockedAccount.storage` is a `map` instead of an `unordered_map`, as ordering is important for fuzzing.
- `MockedAccount.nonce` is `uint64_t` to match go-qrl's account nonce range.
- A spelling mistake in a `qrvmc.h` comment is fixed for the codespell check.
