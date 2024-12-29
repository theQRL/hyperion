contract C {
    function f() public pure {
        // We allow an exception for 0
        address payable a = payable(0);
        a = payable(address(1));
        address payable b = payable(Z0123456789012345678901234567890123456789);
        b = payable(Z9876543210987654321098765432109876543210);
    }
}
// ----
