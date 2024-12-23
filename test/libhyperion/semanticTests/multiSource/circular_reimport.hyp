==== Source: s1.hyp ====
import {f as g, g as h} from "s2.hyp";
function f() pure returns (uint) { return 100 + h() - g(); }
==== Source: s2.hyp ====
import {f as h} from "s1.hyp";
function f() pure returns (uint) { return 2; }
function g() pure returns (uint) { return 4; }
==== Source: s3.hyp ====
import "s1.hyp";
contract C {
  function foo() public pure returns (uint) {
    return f() - g() - h();
  }
}
// ----
// foo() -> 0x60
