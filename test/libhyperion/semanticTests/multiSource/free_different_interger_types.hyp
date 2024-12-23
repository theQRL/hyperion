==== Source: s1.hyp ====
function f(uint24) pure returns (uint) { return 24; }
function g(bool) pure returns (bool) { return true; }
==== Source: s2.hyp ====
import {f as g, g as g} from "s1.hyp";
contract C {
  function foo() public pure returns (uint, bool) {
    return (g(2), g(false));
  }
}
// ----
// foo() -> 24, true
