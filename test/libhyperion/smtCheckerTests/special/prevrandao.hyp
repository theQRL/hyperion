contract C
{
	function f(uint prevrandao) public view {
		assert(block.prevrandao == prevrandao); // should fail
	}
}
// ====
// SMTEngine: all
// SMTIgnoreCex: yes
// ----
// Warning 6328: (58-96): CHC: Assertion violation happens here.
