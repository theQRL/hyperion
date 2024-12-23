==== Source: s1.hyp ====


uint constant a = 89;

function fre() pure returns (uint) {
    return a;
}

==== Source: s2.hyp ====

import {a as b, fre} from "s1.hyp";
import "s1.hyp" as M;

uint256 constant a = 13;

contract C {
    function f() public returns (uint, uint, uint, uint) {
        return (a, fre(), M.a, b);
    }
}
// ----
// f() -> 0x0d, 0x59, 0x59, 0x59
