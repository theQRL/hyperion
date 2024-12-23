==== Source: A ====
import "s1.hyp" as M;
function f(uint _x) pure {
	assert(_x > 0);
}
contract D {
	function g(uint _y) public pure {
		M.f(200); // should hold
		M.f(_y); // should fail
		f(10); // should hold
		f(_y); // should fail
	}
}
==== Source: s1.hyp ====
function f(uint _x) pure {
	assert(_x > 100);
}
// ====
// SMTEngine: all
// SMTIgnoreOS: macos
// ----
// Warning 6328: (A:50-64): CHC: Assertion violation happens here.\nCounterexample:\n\n_y = 0\n\nTransaction trace:\nD.constructor()\nD.g(0)\n    s1.hyp:f(200) -- internal call\n    s1.hyp:f(0) -- internal call\n    A:f(10) -- internal call\n    A:f(0) -- internal call
// Warning 6328: (s1.hyp:28-44): CHC: Assertion violation happens here.\nCounterexample:\n\n_y = 0\n\nTransaction trace:\nD.constructor()\nD.g(0)\n    s1.hyp:f(200) -- internal call\n    s1.hyp:f(0) -- internal call
