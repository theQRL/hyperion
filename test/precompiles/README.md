# Precompile test vectors from go-qrl

`depositroot.json`, `mldsa87_verify.json`, `modexp.json` and `xmss_verify.json` are copied unchanged
from go-qrl's `core/vm/testdata/precompiles/` at commit `f3864c46b3f94662c514105da1efaf3466394fae`
(<https://github.com/theQRL/go-qrl>, LGPL-3.0). go-qrl has no vectors for its `shake256` precompile,
so `shake256.json` was generated with `scripts/generate_shake256_vectors.go`, which runs go-qrl's own
precompile at the same commit for the output and the gas. `test/QRVMHostTests.cpp` runs every vector
through the test host's precompile at the same address and checks the output and the gas against
go-qrl's, so that the host cannot drift from the node.

To update them, copy the files from a newer go-qrl commit, regenerate `shake256.json` against it and
change the commit above.
