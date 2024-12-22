contract C {
    function f(uint x) public returns (uint y) {
        assembly { y := shl(2, x) }
    }
}
// ----
// f(uint256): 7 -> 28
