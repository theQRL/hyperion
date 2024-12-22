contract C {
	address coin;
	uint prevrandao;
	uint gas;
	uint number;
	uint timestamp;
	function f() public {
		coin = block.coinbase;
		prevrandao = block.prevrandao;
		gas = block.gaslimit;
		number = block.number;
		timestamp = block.timestamp;

		g();
	}
	function g() internal view {
		assert(uint160(coin) >= 0); // should hold
		assert(prevrandao > 2**64); // should hold
		assert(gas >= 0); // should hold
		assert(number >= 0); // should hold
		assert(timestamp >= 0); // should hold

		assert(coin == block.coinbase); // should fail with BMC
		assert(prevrandao == block.prevrandao); // should fail with BMC
		assert(gas == block.gaslimit); // should fail with BMC
		assert(number == block.number); // should fail with BMC
		assert(timestamp == block.timestamp); // should fail with BMC
	}
}
// ====
// SMTEngine: bmc
// ----
// Warning 4661: (337-363): BMC: Assertion violation happens here.
// Warning 4661: (497-527): BMC: Assertion violation happens here.
// Warning 4661: (555-593): BMC: Assertion violation happens here.
// Warning 4661: (621-650): BMC: Assertion violation happens here.
// Warning 4661: (678-708): BMC: Assertion violation happens here.
// Warning 4661: (736-772): BMC: Assertion violation happens here.
// Info 6002: BMC: 10 verification condition(s) proved safe! Enable the model checker option "show proved safe" to see all of them.
