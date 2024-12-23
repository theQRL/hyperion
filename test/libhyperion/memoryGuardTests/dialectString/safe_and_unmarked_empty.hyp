contract C {
    function f() external pure {
        assembly "zvmasm" ("memory-safe") {}
        assembly {}
    }
}
// ----
// :C(creation) true
// :C(runtime) true
