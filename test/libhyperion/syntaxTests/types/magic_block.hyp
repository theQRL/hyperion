contract C {
    function f() public view returns (address payable) {
        return block.coinbase;
    }
    function g() public view returns (uint) {
        return block.gaslimit;
    }
    function h() public view returns (uint) {
        return block.timestamp;
    }
    function i() public view returns (uint) {
        return block.chainid;
    }
    function j() public view returns (uint) {
        return block.basefee;
    }
    function k() public view returns (uint ret) {
        assembly {
            ret := basefee()
        }
    }
    function l() public view returns (uint) {
        return block.prevrandao;
    }
    function m() public view returns (uint ret) {
        assembly {
            ret := prevrandao()
        }
    }
}
// ----
