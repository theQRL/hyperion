// SPDX-License-Identifier: GPL-3.0
// Generate test/precompiles/shake256.json, vectors for the test host's shake256 precompile in the
// format of go-qrl's core/vm/testdata/precompiles/*.json, by running go-qrl's own precompile
// (address 6) for the output and the gas. Run in a temporary Go module requiring the go-qrl commit
// recorded in test/precompiles/README.md:
//
//	go mod init vectors
//	go get github.com/theQRL/go-qrl@f3864c46b3f94662c514105da1efaf3466394fae
//	go run /path/to/hyperion/scripts/generate_shake256_vectors.go > /path/to/hyperion/test/precompiles/shake256.json
package main

import (
	"encoding/hex"
	"encoding/json"
	"fmt"
	"os"

	"github.com/theQRL/go-qrl/common"
	"github.com/theQRL/go-qrl/core/vm"
)

type vector struct {
	Input       string
	Expected    string
	Gas         uint64
	Name        string
	NoBenchmark bool
}

// pattern returns length bytes counting up from offset, wrapping at 256.
func pattern(length int, offset byte) []byte {
	b := make([]byte, length)
	for i := range b {
		b[i] = byte(i) + offset
	}
	return b
}

// main runs the shake256 precompile over a fixed set of inputs and prints the vectors as JSON.
func main() {
	precompile := vm.PrecompiledContractsZond[common.BytesToAddress([]byte{6})]
	inputs := []struct {
		name string
		data []byte
	}{
		{"shake256_empty", nil},
		{"shake256_abc", []byte("abc")},
		{"shake256_quick_brown_fox", []byte("The quick brown fox jumps over the lazy dog")},
		{"shake256_63_bytes", pattern(63, 0)},
		{"shake256_64_bytes", pattern(64, 0)},
		{"shake256_65_bytes", pattern(65, 0)},
		{"shake256_128_bytes", pattern(128, 1)},
		{"shake256_200_bytes", pattern(200, 2)},
		{"shake256_1000_bytes", pattern(1000, 3)},
	}
	var vectors []vector
	for _, input := range inputs {
		output, err := precompile.Run(input.data)
		if err != nil {
			panic(err)
		}
		vectors = append(vectors, vector{
			Input:    hex.EncodeToString(input.data),
			Expected: hex.EncodeToString(output),
			Gas:      precompile.RequiredGas(input.data),
			Name:     input.name,
		})
	}
	out, err := json.MarshalIndent(vectors, "", "  ")
	if err != nil {
		panic(err)
	}
	fmt.Fprintln(os.Stdout, string(out))
}
