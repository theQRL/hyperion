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

		assert(coin == block.coinbase); // should hold with CHC
		assert(prevrandao == block.prevrandao); // should hold with CHC
		assert(gas == block.gaslimit); // should hold with CHC
		assert(number == block.number); // should hold with CHC
		assert(timestamp == block.timestamp); // should hold with CHC

		assert(coin == address(this)); // should fail
	}
}
// ====
// SMTEngine: chc
// SMTIgnoreOS: macos
// ----
// Warning 6328: (932-961): CHC: Assertion violation happens here.
// Info 1391: CHC: 12 verification condition(s) proved safe! Enable the model checker option "show proved safe" to see all of them.
