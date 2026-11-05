# Precompile test vectors from go-qrl

`depositroot.json` and `modexp.json` are copied unchanged from go-qrl's
`core/vm/testdata/precompiles/` at commit `f3864c46b3f94662c514105da1efaf3466394fae`
(<https://github.com/theQRL/go-qrl>, LGPL-3.0). `test/QRVMHostTests.cpp` runs every vector through
the test host's precompile at the same address and checks the output and the gas against go-qrl's,
so that the host cannot drift from the node.

To update them, copy the files from a newer go-qrl commit and change the commit above.
