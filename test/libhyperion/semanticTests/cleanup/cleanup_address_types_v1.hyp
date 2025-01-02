pragma abicoder v1;
// Checks that address types are properly cleaned before they are compared.
contract C {
    function f(address a) public returns (uint256) {
        if (a != Z1234567890123456789012345678901234567890) return 1;
        return 0;
    }

    function g(address payable a) public returns (uint256) {
        if (a != Z1234567890123456789012345678901234567890) return 1;
        return 0;
    }
}
// ====
// ABIEncoderV1Only: true
// compileViaYul: false
// ----
// f(address): Zffff1234567890123456789012345678901234567890 -> 0x0 # We input longer data on purpose.#
// g(address): Zffff1234567890123456789012345678901234567890 -> 0x0
