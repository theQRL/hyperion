contract C {
    function f() external pure {
        /// @hyperion memory-safe-assembly
        assembly {}
        assembly { mstore(0,0) }
    }
}
// ----
// :C(creation) true
// :C(runtime) false
