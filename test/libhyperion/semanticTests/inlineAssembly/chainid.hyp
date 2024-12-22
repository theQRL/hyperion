contract C {
    function f() public returns (uint id) {
        assembly {
            id := chainid()
        }
    }
}
// ----
// f() -> 1
