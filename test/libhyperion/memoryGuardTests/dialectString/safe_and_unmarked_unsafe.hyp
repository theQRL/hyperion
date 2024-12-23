contract C {
    function f() external pure {
        assembly "zvmasm" ("memory-safe") {}
        assembly { mstore(0,0) }
    }
}
// ----
// :C(creation) true
// :C(runtime) false
