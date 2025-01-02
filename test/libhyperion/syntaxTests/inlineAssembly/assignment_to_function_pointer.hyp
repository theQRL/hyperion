contract C {
    function f() public pure {
        function() external g;
        assembly {
            // TODO(rgeraldes24)
            g.address := 0x42
            g.selector := 0x23
        }
    }
}
