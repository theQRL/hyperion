### Unreleased

Breaking Changes:
 * Code Generator: Use `Panic(uint512)` with selector `0xdf75fb3b` for compiler-generated panics and `catch Panic`. The payload remains a 4-byte selector followed by a 64-byte code. Contracts compiled by 0.2.0 retain the `0x4e487b71` selector.
 * Type System: Bare `int` and `uint` now mean `int512` and `uint512`, the full QRVM word. Explicitly sized integers keep their declared widths. This changes ABI signatures, selectors and storage layouts containing bare integers.
 * Type System: Array and fixed-bytes indices, calldata slice bounds, allocation lengths and generated array getter indices accept full-word integers. `catch Panic(uint code)` binds a `uint512` code.

Important Bugfixes:
 * Code Generator: Fix `delete` applied to an element of a `bytes` array in memory zeroing the whole 64-byte word starting at the element's location instead of only the element's byte, in the legacy code generator (upstream SOL-2026-5).
 * Yul Optimizer: Fix knowledge about storage/memory address relations going stale in the `DataFlowAnalyzer` after a variable is reassigned in Yul, which could cause `LoadResolver` to replace loads with outdated values and `EqualStoreEliminator` to remove needed stores (upstream SOL-2026-7).

Bugfixes:
 * Code Generator: Convert `mldsa87verify` and `xmssverify` arguments in evaluation order in the IR code generator, so a storage `bytes` argument is copied to memory before later arguments can modify it, as the legacy code generator and the SMTChecker already do.

Build System:
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
