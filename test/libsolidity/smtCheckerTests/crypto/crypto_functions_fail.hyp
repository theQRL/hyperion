contract C {
	function k(bytes memory b0, bytes memory b1) public pure {
		bytes32 k0 = keccak256(b0);
		bytes32 k1 = keccak256(b1);
		assert(k0 == k1);
	}
	function s(bytes memory b0, bytes memory b1) public pure {
		bytes32 s0 = sha256(b0);
		bytes32 s1 = sha256(b1);
		// Disabled because of Spacer nondeterminism.
		//assert(s0 == s1);
	}
	function e(bytes memory p0, bytes memory w0, bytes memory a0, bytes memory s0, bytes memory p1, bytes memory w1, bytes memory a1, bytes memory s1) public pure {
		bytes32 r0 = depositroot(p0, w0, a0, s0);
		bytes32 r1 = depositroot(p1, w1, a1, s1);
		// Disabled because of Spacer nondeterminism.
		//assert(r0 == r1);
	}
}
// ====
// SMTEngine: all
// SMTIgnoreCex: yes
// ----
// Warning 2072: (218-228): Unused local variable.
// Warning 2072: (245-255): Unused local variable.
// Warning 2072: (507-517): Unused local variable.
// Warning 2072: (551-561): Unused local variable.
// Warning 6328: (135-151): CHC: Assertion violation happens here.
