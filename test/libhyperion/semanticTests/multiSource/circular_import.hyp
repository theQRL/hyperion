==== Source: s1.hyp ====
import {f as g} from "s2.hyp";
function f() pure returns (uint) { return 1; }
==== Source: s2.hyp ====
import {f as g} from "s1.hyp";
function f() pure returns (uint) { return 2; }
contract C {
  function foo() public pure returns (uint) {
    return f() - g();
  }
}
// ----
// foo() -> 1
