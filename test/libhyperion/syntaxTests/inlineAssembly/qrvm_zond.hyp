contract C {
    function f() pure external {
        assembly {
            let s := returndatasize()
            returndatacopy(0, 0, s)
        }
    }
    function g() view external returns (uint ret) {
        assembly {
            ret := staticcall(0, gas(), 0, 0, 0, 0)
        }
    }
    function h() view external returns (uint ret) {
        assembly {
            ret := shl(gas(), 5)
            ret := shr(ret, 2)
            ret := sar(ret, 2)
        }
    }
    function i() external returns (address ret) {
        assembly {
            ret := create2(0, 0, 0, 0)
        }
    }
    function j() view external returns (bytes32 ret) {
        assembly {
            ret := extcodehash(address())
        }
    }
    function k() view external returns (uint id) {
        assembly {
            id := chainid()
        }
    }
    function l() view external returns (uint sb) {
        assembly {
            sb := selfbalance()
        }
    }
}
// ----
