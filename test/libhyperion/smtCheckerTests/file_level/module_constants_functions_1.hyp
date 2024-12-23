==== Source: s1.hyp ====
uint constant a = 89;

function fre() pure returns (uint) {
	return a;
}

==== Source: s2.hyp ====
function foo() pure returns (uint) {
	return 42;
}

==== Source: s3.hyp ====
import {fre as foo} from "s1.hyp";
import "s1.hyp" as M;
import "s2.hyp" as N;

uint256 constant a = 13;

contract C {
	function f() internal pure returns (uint, uint, uint, uint) {
		return (a, foo(), N.foo(), M.a);
	}
	function p() public pure {
		(uint x, uint y, uint z, uint t) = f();

		assert(x == 13); // should hold
		assert(x == 89); // should fail

		assert(y == 89); // should hold
		assert(y == 42); // should fail

		assert(z == 42); // should hold
		assert(z == 89); // should fail

		assert(t == 89); // should hold
		assert(t == 13); // should fail
	}
}
// ====
// SMTEngine: all
// ----
// Warning 6328: (s3.hyp:327-342): CHC: Assertion violation happens here.
// Warning 6328: (s3.hyp:396-411): CHC: Assertion violation happens here.
// Warning 6328: (s3.hyp:465-480): CHC: Assertion violation happens here.
// Warning 6328: (s3.hyp:534-549): CHC: Assertion violation happens here.
// Info 1391: CHC: 4 verification condition(s) proved safe! Enable the model checker option "show proved safe" to see all of them.
