==== Source: s1.hyp ====
function f(uint) pure returns (uint) { return 24; }
function g() pure returns (bool) { return true; }
==== Source: s2.hyp ====
import {f as g, g as g} from "s1.hyp";
contract C {
  function foo() public pure returns (uint, bool) {
    return (g(2), g());
  }
}
// ----
