contract C {
	function k(bytes memory b0) public pure {
		bytes memory b1 = b0;
		bytes32 k0 = keccak256(b0);
		bytes32 k1 = keccak256(b1);
		assert(k0 == k1);
	}
	function s(bytes memory b0) public pure {
		bytes memory b1 = b0;
		bytes32 s0 = sha256(b0);
		bytes32 s1 = sha256(b1);
		assert(s0 == s1);
	}
	function d(bytes memory p0, bytes memory w0, bytes memory a0, bytes memory s0) public pure {
		(bytes memory p1, bytes memory w1, bytes memory a1, bytes memory s1) = (p0, w0, a0, s0);
		bytes32 r0 = depositroot(p0, w0, a0, s0);
		bytes32 r1 = depositroot(p1, w1, a1, s1);
		assert(r0 == r1);
	}
}
// ====
// SMTEngine: all
// ----
// Info 1391: CHC: 3 verification condition(s) proved safe! Enable the model checker option "show proved safe" to see all of them.
