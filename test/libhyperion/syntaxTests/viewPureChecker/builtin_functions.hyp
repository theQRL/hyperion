contract C {
    function f() public {
        payable(this).transfer(1);
        require(payable(this).send(2));
        (bool success,) = address(this).delegatecall("");
        require(success);
		(success,) = address(this).call("");
        require(success);
    }
    function g() pure public {
        bytes32 x = keccak256("abc");
        bytes32 y = sha256("abc");
        bytes32 z = depositroot("abc", "abc", "abc", "abc");
        require(true);
        assert(true);
        x; y; z;
    }
    receive() payable external {}
}
// ----
