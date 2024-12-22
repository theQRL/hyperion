contract C {
    function f() public returns (bool) {
        (bool success, ) = address(4).call("");
        return success;
    }
}
// ----
// f() -> true
