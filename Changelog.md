### Unreleased

Breaking Changes:
 * Builtins: Widen all `addmod` and `mulmod` parameters and return values to `uint` (`uint512`). Narrower unsigned inputs widen implicitly; assigning the result to a narrower type requires an explicit conversion. Intermediate arithmetic remains exact even when it exceeds 512 bits.
 * Code Generator: Use `Panic(uint512)` with selector `0xdf75fb3b` for compiler-generated panics and `catch Panic`. The payload remains a 4-byte selector followed by a 64-byte code. Contracts compiled by 0.2.0 retain the `0x4e487b71` selector.
 * Type System: Bare `int` and `uint` now mean `int512` and `uint512`, the full QRVM word. Explicitly sized integers keep their declared widths. This changes ABI signatures, selectors and storage layouts containing bare integers.
 * Type System: Use full-word `uint` for call value and gas options, `msg.value` and account balances. Narrower unsigned inputs widen implicitly; narrowing message values or balances requires an explicit conversion.
 * Type System: Array and fixed-bytes indices, calldata slice bounds, allocation lengths and generated array getter indices accept full-word integers. `catch Panic(uint code)` binds a `uint512` code.

Important Bugfixes:
 * Code Generator: Mask packed internal function pointers before testing for zero in the legacy pipeline, so calling an unset pointer reverts with `Panic(0x51)` even when a nonzero neighbour shares its storage slot (CIPH-HYPERION-7).
 * Code Generator: Fix `delete` applied to an element of a `bytes` array in memory zeroing the whole 64-byte word starting at the element's location instead of only the element's byte, in the legacy code generator (upstream SOL-2026-5).
 * Yul Optimizer: Fix knowledge about storage/memory address relations going stale in the `DataFlowAnalyzer` after a variable is reassigned in Yul, which could cause `LoadResolver` to replace loads with outdated values and `EqualStoreEliminator` to remove needed stores (upstream SOL-2026-7).

Bugfixes:
 * C API: Make `hyperion_reset()` discard the state of a compilation that a JavaScript exception unwinding through the Emscripten build abandoned without running its destructors; until then every later `hyperion_compile()` failed with an internal error about another `CompilerStack`. The memory of the abandoned compilation is not reclaimed, since its destructors cannot be run safely afterwards; an embedder that can load a fresh module instance should prefer that (CIPH-HYPERION-16).
 * Code Generator: Clean narrowed and wrapped integer arguments when widening them for `addmod` and `mulmod` in the legacy code generator.
 * Code Generator: Clean narrowed and wrapped integer arguments when widening them for call value and gas options in the legacy code generator.
 * Code Generator: Convert `mldsa87verify` and `xmssverify` arguments in evaluation order in the IR code generator, so a storage `bytes` argument is copied to memory before later arguments can modify it, as the legacy code generator and the SMTChecker already do.
 * Parser: Lower the recursion limit in the Emscripten build from 1200 to 256 guarded calls (about 50 nested parentheses or calls), because deeper nesting exhausts the JavaScript engine's stack long before the native limit is reached, and the resulting `RangeError` left the compiler unusable for the rest of the process (CIPH-HYPERION-16).
 * SMTChecker: Model message values and account balances as full-word integers.

Build System:
 * Emscripten: Set the wasm stack size to 5 MB explicitly (the default up to Emscripten 3.1.26, 64 KB from 3.1.27) and check the stack pointer on every assignment (`STACK_OVERFLOW_CHECK=2`), so that recursion which exhausts the stack aborts instead of corrupting memory (CIPH-HYPERION-16, CIPH-HYPERION-17). Export `stackSave` and `stackRestore`, which hypc-js uses to put the stack pointer back after an exception has unwound through the compiler.
 * Tests: Re-enable 47 Zond semantic regressions, extend dual-pipeline coverage, require explanations for single-pipeline fixtures, and report excluded tests as skipped. Add differential integer cleanup tests for every supported width and 512-bit opcode boundaries (CIPH-HYPERION-13).
 * Build with current Apple toolchains (Apple Clang 21, libc++ 22), Boost 1.88+ (Boost.Process v1 headers) and Boost 1.89+ (no compiled Boost.System library).
 * Raise the macOS deployment target from 10.14 to 11.0, the lowest that libc++ 22 supports. The released `hypc-macos` binary requires macOS 11 or later.

### 0.2.0 (2026-10-19)

Breaking Changes:
 * ABI coder: Remove v1 support. All contracts use ABI coder v2; `pragma abicoder v1` is rejected.
 * QRVM: Use 512-bit (64-byte) VM words and 64-byte addresses.

Language Features:
 * Add the `xmssverify` and `mldsa87verify` builtins, which verify XMSS and ML-DSA-87 signatures through precompiles.

Bugfixes:
 * Test host: Verify real ML-DSA-87 signatures and reject weak public keys, matching go-qrl.
 * SMTChecker: Apply digest conversions to `bytes64` when modeling `mldsa87verify`, preventing incorrect proofs and errors on string literals.

Build System:
 * Publish a static Linux arm64 binary, `hypc-static-linux-arm`.

### 0.1.0 (2025-02-22)

 * Version number change only: the compiler is the same as 0.0.1.

### 0.0.2 (2025-02-21)

 * Version number change only: the compiler is the same as 0.0.1.

### 0.0.1 (2025-02-20)

 * First release, forked from Solidity 0.8.23 for the Zond QRVM.
