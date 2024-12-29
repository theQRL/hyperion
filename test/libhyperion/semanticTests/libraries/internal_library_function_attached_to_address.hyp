library L {
    function equals(address a, address b) internal pure returns (bool) {
        return a == b;
    }
}

contract C {
    using L for address;

    function foo(address a, address b) public returns (bool) {
        return a.equals(b);
    }
}
// ----
// foo(address,address): Z111122223333444455556666777788889999aAaa, Z111122223333444455556666777788889999aAaa -> true
// foo(address,address): Z111122223333444455556666777788889999aAaa, Z0000000000000000000000000000000000000000 -> false
